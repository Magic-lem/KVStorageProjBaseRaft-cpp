# 分布式一致性算法：Raft学习

## 1 什么是分布式系统？

分布式系统是由一组通过网络进行通信、为了完成共同的任务而协调工作的计算机节点组成的系统。这些节点可能位于不同的物理位置，但它们协同工作以提供一个统一的计算平台或服务。分布式系统的出现是为了用廉价的、普通的机器完成单个计算机无法完成的计算、存储任务。其目的是利用更多的机器，处理更多的数据。例如，分布式计算系统、分布式存储系统（缓存、数据库、文件）等。

> 分布式和集群的区别
>
> - 分布式是指**多个系统协同合作完成一个特定任务的系统**。是**不同的系统**部署在不同的服务器上，服务器之间相互调用。主要工作是分解任务，把职能拆解，解决中心化管理。
> - 集群是指**在几个服务器上部署相同的应用程序来分担客户端的请求**。是**同一个系统**部署在不同的服务器上，比如一个登陆系统部署在不同的服务器上。使用场景是为了分担请求的压力。

![98ef710d2882cb08df2a48562547178c.png](F:\学习\研二\学习\cpp项目\figures\分布式示例.png)

**分布式系统面临的挑战：**

- 网络延迟和分区

  节点间通过网络通信，而网络是不可靠的。可能的网络问题包括：网络分割、延时、丢包、乱序，网络的不可靠性和延迟可能导致节点之间的通信问题理。
- 普遍的节点故障

  虽然单个节点的故障概率较低，但节点数目达到一定规模，出故障的概率就变高了。分布式系统需要保证故障发生的时候，系统仍然是可用的，这就需要监控节点的状态，在节点故障的情况下将该节点负责的计算、存储任务转移到其他节点。
- 异构的机器与网络

  分布式系统中的机器，配置不一样，其上运行的服务也可能由不同的语言、架构实现，因此处理能力也不一样；节点间通过网络连接，而不同网络运营商提供的网络的带宽、延时、丢包率又不一样。怎么保证大家齐头并进，共同完成目标，这是个不小的挑战。

## 2 CAP理论 和 BASE理论

### 2.1 CAP 理论

- **C consistency 一致性**
- 在分布式系统中有多个节点，整个系统对外提供的服务应该是一致的。即用户**在不同的系统节点访问数据的时候应该是同样的结果**，不能出现1号节点是结果1， 2号节点是结果2这种不一致的情况。
- **A availability 可用性**

  分布式系统为用户提供服务，需要保证能够**在一些节点异常的情况下仍然支持为用户提供服务**。
- **P partition tolerance 分区容错性（容灾）**
- 分布式系统的部署可能跨省，甚至跨国。不同省份或者国家之间的服务器节点是通过网络进行连接，此时如果**两个地域之间的网络连接断开，整个分布式系统的体现就是分区容错性了**。在这种系统出现网络分区的情况下系统的服务就**需要在一致性 和 可用性之间进行取舍**。要么保持一致性，返回错误。要么是保证可用性，使得两个地域之间的分布式系统不一致。

![image (4)](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\CAP.png)

==**CAP理论一定是无法全部满足三者，只能满足其中的两者（CA、CP、AP）。**==

对于一个分布式系统而言，**`<font color='red'>`P是分布式的前提，必须保证`</font>`，因为只要有网络交互就一定会有延迟和数据丢失，这种状况我们必须接受，必须保证系统不能挂掉**。所以只剩下C、A可以选择。要么保证数据一致性（保证数据绝对正确），要么保证可用性（保证系统不出错）。

当选择了C（一致性）时，如果由于网络分区而无法保证特定信息是最新的，则系统将`<font color='cornflowerblue'>`**返回错误或超时**`</font>`。

当选择了A（可用性）时，系统将始终处理客户端的查询并尝试返回**最新的`<font color='cornflowerblue'>`可用的信息版本`</font>`**，即使由于网络分区而**无法保证其是最新的**。

### 2.2 BASE 理论

## 3 分布一致性：强一致性和弱一致性

### 3.1 分布一致性概述

![image (5)](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\分布一致性.png)

分布式一致性是指在**分布式环境**中对某个副本数据进行更新操作时，必须**确保其他副本也会更新**，避免不同副本数据不一致。

总结来讲，分布式一致性就是为了解决以下两个问题：

- `数据不能存在单个节点（主机）上，否则可能出现单点故障。`
- `多个节点（主机）需要保证具有相同的数据。`

分布一致性可以分为弱一致性和强一致性：

- **弱一致性 **（例如：DNS域名系统）

  弱一致性体现的是最终一致性，即如上CAP理论中 ，两个地域的请求分别通过两地的节点写入，可以不用立即进行同步，而是**经过一段时间之后两地的用户数据变为一致**。这种一致性成为弱一致性，即最终一致性。 也就是用户一地写入之后**从另外一个节点读取，无法立即读到刚才写入的数据**。
- **强一致性 **（又被称为共识，例如：银行系统）

  强一致性描述的是一个请求在一个节点写入之后在其他节点读取，则**该数据的更新能够被立刻读到**。

### 3.2 强一致性和弱一致性

强一致性和弱一致性只是一种统称，按照**从强到弱**，可以划分为

- 线性一致性Linearizability consistency ，也叫原子性
- 顺序一致性 Sequential consistency
- 因果一致性 Causal consistency
- 最终一致性 Eventual consistency

**==强一致性包括线性一致性和顺序一致性，其他的如最终一致都是弱一致性。==**

### 3.3 顺序一致性

> 虽然强度上 线性一致性 > 顺序一致性，但因为顺序一致性出现的时间比较早(1979年)，线性是在顺序的基础上的加强(1990 年)。因此先介绍下 顺序一致性。

顺序一致性最早是用来描述多核 CPU 的行为：如果可以找到一个所有 CPU 执行指令的排序，该排序中**每个 CPU 要执行指令的顺序得以保持**，且实际的 **CPU 执行结果与该指令排序的执行结果一致**，则称该次执行达到了顺序一致性。例如：

![img](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\Sequential-Consistency.svg)

我们找到了指令的一个排序，排序中各个 CPU 的指令顺序得以保持（如 `C: R(X, 1)` 在 `C: R(X, 2)` 之前），这个排序的执行结果与 CPU 分开执行的结果一致，因此该 CPU 的执行是满足顺序一致性的。

`<font color='red'>`**顺序一致性关心的是一个 CPU 内部执行指令的顺序，而不关心 CPU 之间的相对顺序。**`</font>`

**反例：**

![img](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\Sequential-Consistency-swap-2.svg)

上图的系统，实际上是找不到一个全局的排序来满足顺序一致性的需求的。根本上，从 C 的顺序推导出 X 的写入顺序为 `1 -> 2`，而同时由 D 推出写入顺序为 `2 -> 1` ，二者矛盾。

因此，拓展到**分布式算法中，顺序一致性的可以理解为**：所有操作的结果看起来好像是**`<font color='red'>`按某个全局顺序执行`</font>`**的，并且**`<font color='red'>`这个全局顺序必须与每个进程（或客户端）本身的操作顺序一致`</font>`**。

> **举个例子：**
>
> 如果有两个进程P1和P2，分别执行以下操作：
>
> - P1: 写(x, 1), 写(y, 1)
> - P2: 读(y), 读(x)
>
> 顺序一致性确保所有进程看到的操作顺序是一致的，但**并不要求所有读操作看到最新的写操作**。也就是通过这两个进程，我可以得到一个全局顺序：
>
> 写(x, 1) ->  读(y) -> 读(x) -> 写(y, 1)
>
> 每个进程也是满足这个顺序的，这就满足了顺序一致性。但这样会导致读(y)操作读取得到的是旧值。

### 3.4 线性一致性

线性一致性又被称为强一致性、严格一致性、原子一致性。是程序能实现的最高的一致性模型，也是分布式系统用户最期望的一致性。CAP 中的 C 一般就指它

顺序一致性中进程只关心大家认同的顺序一样就行，不需要与全局时钟一致，线性就更严格，从这种偏序（partial order）要达到全序（total order）。要求是：

- 任何一次读都能读到某个数据的**最近一次写的数据**。
- 系统中的所有进程，看到的操作顺序，都**与全局时钟（实际时钟）下的顺序一致**。



> 建议阅读：[7.6 线性一致（Linearizability） | MIT6.824 (gitbook.io)](https://mit-public-courses-cn-translatio.gitbook.io/mit6-824/lecture-07-raft2/7.6-qiang-yi-zhi-linearizability)
>
> 对理解线性一致性非常有帮助

## 4 强一致性（共识）算法

**共识需要满足的性质：**

- `<font color='cornflowerblue'>`**在非拜占庭条件下保证共识的一致性（C）**`</font>`。即满足线性一致性。非拜占庭条件就是可信的网络条件，与你通信的节点的信息都是真实的，不存在欺骗。
- `<font color='cornflowerblue'>`**在多数节点存活时，保持可用性（A）**`</font>`。“多数”永远指的是配置文件中所有节点的多数，而不是存活节点的多数。多数等同于超过半数的节点，多数这个概念概念很重要，贯穿Raft算法的多个步骤。
- `<font color='cornflowerblue'>`**不依赖于绝对时间**`</font>`。理解这点要明白共识算法是要应对节点出现故障的情况，在这样的环境中网络报文也很可能会受到干扰从而延迟，如果完全依靠于绝对时间，会带来问题，Raft用自定的**Term（任期）作为逻辑时钟来代替绝对时间。**
- `<font color='cornflowerblue'>`**在多数节点一致后就返回结果**`</font>`，而不会受到个别慢节点的影响。这点与第二点联合理解，只要**“大多数节点同意该操作”就代表整个集群同意该操作**。对于raft来说，**”操作“是储存到日志log中**，一个操作就是log中的一个entry。

==**常见的强一致性算法：**主从同步、多数派、Paxos、Raft、ZAB==

## 5 Raft算法

### 5.1 算法概述

![image (6)](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\raft概述.png)

Raft算法又被称为基于**日志复制**的一致性算法，旨在解决分布式系统中多个节点之间的数据一致性问题。它通过选举一个**`领导者（Leader）`**，让 `领导者`负责管理和协调日志复制，确保所有节点的数据一致。

- **复制日志**

  在分布式系统中，每个节点都维护着一份日志，记录系统操作的历史。为了保证数据一致性，这些日志需要在所有节点之间保持同步。	Raft通过`<font color='cornflowerblue'>`**领导者选举和日志复制机制**`</font>`，确保所有节点的日志最终是一致的。
- **心跳机制与选举**

  Raft使用`<font color='cornflowerblue'>`**心跳机制**`</font>`来触发选举。当系统启动时，每个节点（Server）的初始状态都是追随者（Follower）。每个Server都有一个定时器，超时时间为选举超时（Election Timeout），一般为150-300毫秒。如果一个Server在超时时间内没有收到来自领导者或候选者的任何消息，定时器会重启，并开始一次选举。
- **选举过程（投票）**

  当一个追随者节点发现自己`<font color='cornflowerblue'>`**超过选举超时没有收到领导者的消息，就会变为候选者（Candidate），并开始新一轮选举**`</font>`。候选者节点会增加自己的任期号，并向其他节点发送选票请求。每个节点只能在一个任期内投一票，并且通常会**将票投给第一个请求投票的候选者**。如果一个候选人在收到足够多的选票后，就成为新的领导者。
- **多个候选者（选举失败—>重来）**

  在选举过程中，可能会出现多个候选者同时竞争领导者的位置。这时，如果某个候选者无法在选举超时前获得大多数节点的支持，**选举就会失败**。失败后，所有候选者会`<font color='cornflowerblue'>`**重置自己的定时器，并在下一轮超时后再次发起选举**`</font>`，直到选出新的领导者为止。

### 5.2 工作模式：Leader-Follower 模式

- `<font color='red'>Leader` `</font>`一个集群只有一个 `leader`，不断地向集群其它节点发号施令(心跳、日志同步)，其它节点接到领导者日志请求后成为其追随者
- `<font color='red'>Follower``</font>` 一个服从 `leader`决定的角色，追随领导者，接收领导者日志，并实时同步
- `<font color='red'>Cadidate` `</font>`当 `follower`发现集群没有 `leader`，会重新选举 `leader`，参与选举的节点会变成 `candidate`

### 5.3 重要概念

- **`Raft状态`：**用于维持和管理 Raft 协议操作的内部数据，如日志条目、当前任期、投票纪录、已提交索引、最后应用索引等
- **`状态机`：**raft的上层应用，例如k-v数据库
- **`日志log`：**raft保存的外部命令是以日志保存
- **`term 任期`：**Term作为内部的逻辑时钟，使用Term的对比来比较日志、身份、心跳的新旧而不是用绝对时间
- **`提交日志 commit`：**raft保存日志后，经过复制同步，才能真正应用到上层状态机，这个“应用”的过程为提交
- **节点身份 `follower、Candidate、Leader` ：**raft集群中不同节点的身份
- **`选举`：**follower变成leader需要选举
- **`心跳、日志同步`：**leader向follower发送心跳（AppendEntryRPC）用于告诉follower自己的存在以及通过心跳来携带日志以同步
- **`日志的term`：**在日志提交的时候，会记录这个日志在什么“时候”（哪一个term）记录的，用于后续日志的新旧比较
- **`日志条目 entry`：**日志条目是日志的**基本单元**。每个日志条目记录了一次状态变化或一个命令。日志条目包含了如下几个关键信息：
  - 索引（Index）：该日志条目在日志中的位置。（索引是连续的，不会因为Term改变而改变）
  - 任期（Term）：该日志条目被创建时的任期号。
  - 命令（Command）：要应用于状态机的具体命令或操作。
- **`日志截断（log truncation）`：**在发现日志不匹配时，从某个索引位置开始删除现有日志条目，并用新的日志条目替换这些位置的内容。这是一种确保日志一致性的方法。

### 5.4 日志 log

日志中保存`<font color='cornflowerblue'>`**客户端发送来的命令**`</font>`，上层的状态机根据日志执行命令，那么**日志一致，自然上层的状态机就是一致的**。结构如下：

![img](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\日志结构.png)

其中，每一个元素被称为一个日志条目 `entry`，每个日志条目包含一个Index、Term以及具体的命令Command。

**核心思想**：Raft算法可以让多个节点的上层状态机保持一致的关键是让==**各个节点的日志保持一致**== **。**

### 5.5 任期 term

- **每个节点都有自己的term，Term用连续的数字进行表示**。Term会在follower发起选举（成为Candidate从而试图成为Leader ）的时候**加1**，对于一次选举可能存在两种结果：

  - 胜利当选：超过半数的节点认为当前 `Candidate`有资格成为 `Leader`，即超过半数的节点给当前 `Candidate`投了选票。
  - 失败：如果没有任何 `Candidate`（一个Term的 `Leader`只有一位，但是如果多个节点同时发起选举，那么某个Term的Candidate可能有多位）获得超半数的选票，那么选举超时之后又会**开始另一个Term（Term递增）的选举**。
- 对于节点，当发现**自己的Term小于其他节点的Term时，这意味着“自己已经过期”**，不同身份的节点的处理方式有所不同：

  - leader、Candidate：退回follower并更新term到较大的那个Term
  - follower：更新Term信息到较大的那个Term
- > 这里解释一下为什么 自己的Term小于其他节点的Term时leader、Candidate会退回follower 而不是延续身份，因为通过Term信息知道自己过期，意味着自己可能发生了网络隔离等故障，那么在此期间整个Raft集群可能已经有了新的leader、 **提交了新的日志** ，此时自己的日志是有缺失的，如果不退回follower，那么可能会导致整个集群的日志缺失，不符合安全性。
  >
- 相反，如果发现自己的Term大于其他节点的Term，那么就会**忽略这个消息**中携带的其他信息（**这个消息是过期的**）。

### 5.6 工作机制

主要有以下四大模块，组成Raft算法：

- **领导者选举**
- **日志同步、心跳**
- **持久化**
- **日志压缩，快照**

### 5.7 领导者选举

> 节点需要通过集群元数据信息与其它节点进行沟通，而沟通的方式是**`RPC请求`**

- ##### 节点之间通过网络通信，其他节点（follower）如何知道leader出现故障？

  leader会定时向集群中剩下的节点（follower）发送AppendEntry（作为心跳，hearbeat ）以通知自己仍然存活。如果follower**在一段时间内没有接收leader发送的AppendEntry**，那么follower就会认为当前的leader 出现故障，从而发起选举。
- ##### AppendEntry的作用？


  1. 心跳
  2. 携带日志 `entry`及其辅助信息，以控制日志的同步和日志向状态机提交
  3. 通告 `leader`的index和term等关键信息以便 `follower`对比确认 `follower`自己或者 `leader`是否过期
- ##### **follower知道leader出现故障后如何选举出leader？**

  系统中的 `追随者(follower)`**未收到日志同步(也可理解为心跳)**转变成为 `候选者(Candidate)`，在成为 `候选者(Candidate)`后，不满足于自己现在状态，迫切的想要成为 `领导者(leader)`，虽然它给自己投了1票，但很显然1票是不够，它需要其它节点的选票才能成为领导者，所以**通过RPC请求其它节点给自己投票**。

  ![img](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\领导者选举.png)
- ##### 符合什么条件的节点可以成为leader？（ 选举限制 ）

  目的：保证选举出的 `leader` 一定包含了整个集群中目前已 `committed` 的所有日志

  方法：当 `candidate` 发送 RequestVoteRPC 时，会带上最后一个 `entry` 的信息。 所有的节点收到该请求后，都会**比对自己的日志，如果发现自己的日志更新一些，则会拒绝投票给该 `candidate**`。

  **如何判断日志的新旧：最新日志entry的term和对应的index。（index即日志entry在整个日志的索引）**


  - term大的日志更加新
  - term相同的日志index大的更加新

### 5.8 日志同步、心跳

- ##### raft日志的两个特点


  - 两个节点的日志中，有两个 entry 拥有相同的 index 和 term，那么它们**一定记录了相同的内容/操作**，即两个日志**匹配**
  - 两个节点的日志中，有两个 entry 拥有相同的 index 和 term，那么它们**前面的日志entry也相同**

    > 如何保证这两点：
    >
    > 保证第一点：仅有 leader 可以生成 entry
    >
    > 保证第二点：leader 在通过 AppendEntriesRPC 和 follower 通讯时，除了带上自己的term等信息外，还会带上entry的index和对应的term等信息，follower在接收到后通过对比就可以知道自己与leader的日志是否匹配，不匹配则拒绝请求。leader发现follower拒绝后就知道entry不匹配，那么下一次就会尝试匹配前一个entry，直到遇到一个entry匹配，并将不匹配的entry给删除（覆盖）。
    >

    > 注意：raft为了避免出现一致性问题，要求 leader 绝不会提交过去的 term 的 entry （即使该 entry 已经被复制到了多数节点上）。leader 永远只提交当前 term 的 entry， 过去的 entry 只会随着当前的 entry 被一并提交。
    >
- ##### raft日志同步方式

  先**找到日志不匹配的那个点，然后`<font color='cornflowerblue'>`只同步那个点之后的日志`</font>`**。

  一个 `follower`，如果 `leader`认为其日志已经和自己匹配了，那么在AppendEntryRPC中不用携带日志（其他信息依然要携带），反之如果follower的日志只有部分匹配，那么就需要在AppendEntryRPC中携带对应的日志。**心跳RPC可以看成是没有携带日志的特殊的日志同步RPC。**

  **日志同步（复制）的过程：**

  ![日志复制](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\日志复制.png)

**-------------------------------------------------------------------------------------------Raft算法的核心步骤👇------------------------------------------------------------------------------------------------**

1. 接收到客户端请求之后，`领导者`会**根据用户指令和自身任期以及日志索引等信息生成一条 `新的日志项`**，并附加到本地日志中。
2. 领导者通过日志复制 RPC，将日志复制到其他跟随者节点。
3. 跟随者将日志附加到本地日志成功之后，便返回 success，此时新的日志项还没有被跟随者提交。
4. 当领导者接收到**大多数（超过集群数量的一半）**跟随者节点的 success 响应之后，便将此日志 `提交(commit)`到它的状态机中。
5. 领导者将执行的结果返回给客户端。
6. 当跟随者收到`<font color='red'>`**下一次领导者的心跳请求或者新的日志复制请求之后**`</font>`，如果发现领导者已经应用了之前的日志，但是它自己还没有之后，那么它便会把这条日志项应用到本地状态机中。（类似于**二阶段提交**的方式）

**-------------------------------------------------------------------------------------------Raft算法的核心步骤👆------------------------------------------------------------------------------------------------**

- ##### 安全性保障

  `Election Safety`：每个 `term` 最多只会有一个 `leader`

  `Leader Append-Only`：`Leader`绝不会覆盖或删除自己的日志，**只会追加**

  `Log Matching`：如果两个日志的 `index` 和 `term` 相同，那么这两个日志相同  —— `raft日志的特点1`

      如果两个日志相同，那么他们之前的日志均相同 ——`raft日志的特点2`

  `Leader Completeness`：一旦一个操作被提交了，那么在之后的 `term` 中，该操作都会存在于日志中

  `State Machine Safety`：状态机一致性，一旦一个节点应用了某个 `index` 的 entry 到状态机，那么其他所有节点应用的该 `index` 的操作都是一致的（各个节点之间一致，使得上层状态机都一致）
- **为什么不直接让follower拷贝leader的日志|leader发送全部的日志给follower？**

  会携带大量的无效的日志（因为这些日志follower本身就有）
- ##### leader如何知道follower的日志是否与自己完全匹配？

  在AppendEntryRPC中携带上entry的 `index`和对应的 `term`（日志的term），可以**通过比较最后一个日志的 `index`和 `term`**来得出某个follower日志是否匹配。
- ##### 如果发现不匹配，那么如何知道哪部分日志是匹配的，哪部分日志是不匹配的呢？

  `leader`每次发送AppendEntryRPC后，`follower`都会根据其 `entry`的 `index`和对应的 `term`来判断某一个日志是否匹配。在leader刚当选，**会从最后一个日志开始判断是否匹配**，如果匹配，那么后续发送AppendEntryRPC就不需要携带日志entry了。

  如果不匹配，那么下一次就发送 **倒数第2个** 日志entry的index和其对应的term来判断匹配，

  如果还不匹配，那么依旧重复这个过程，即发送 倒数第3个 日志entry的相关信息

  **重复这个过程，直到遇到一个匹配的日志。**
- ##### 优化：寻找匹配加速（可选）

  在寻找匹配日志的过程中，在最后一个日志不匹配的话就尝试倒数第二个，然后不匹配继续倒数第三个。。。

  `leader和follower` 日志存在大量不匹配的时候这样会太慢，可以用一些方式一次性的多倒退几个日志，就算回退稍微多了几个也不会太影响，具体实现参考 [7.3 快速恢复（Fast Backup） - MIT6.824 (gitbook.io)](http://gitbook.io/)

### 5.9 持久化

Raft的一大优势就是**Fault Tolerance**（容灾），即能够在部分节点宕机、失联或者出现网络分区的情况下依旧让系统正常运行。为了保证这一点，除了领导选举与日志复制外，还需要定期将一些数据持久化到磁盘中，以实现在服务器重启时利用持久化存储的数据恢复节点上一个工作时刻的状态。持久化的内容仅仅是 `Raft`层, 其应用层不做要求。

![img](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\持久化.png)

**论文中提到需要持久化的数据包括:**

- `voteFor`：

  `voteFor`记录了一个节点在某个 `term`内的投票记录, 因此如果**不将这个数据持久化, 可能会导致如下情况**:

  - 在一个 `Term`内某个节点向某个 `Candidate`投票, 随后故障
  - 故障重启后, 又收到了另一个 `RequestVote RPC`, 由于其`<font color='red'>`没有将 `votedFor`持久化, 因此其不知道自己已经投过票, 结果是再次投票`</font>`, 这将导致同一个 `Term`可能出现2个 `Leader`。
- `currentTerm`:
  `currentTerm`的作用也是实现一个任期内最多只有一个 `Leader`, 因为如果一个节点重启后不知道现在的 `Term`时多少, 其`<font color='red'>`无法再进行投票时将 `currentTerm`递增到正确的值`</font>`, 也可能导致有多个 `Leader`在同一个 `Term`中出现
- `Log`:
  这个很好理解, 需要用 `Log`来**恢复自身的状态**

**为什么只需要持久化 `votedFor`, `currentTerm`, `Log`？**

其他的数据， 包括 `commitIndex`、`lastApplied`、`nextIndex`、`matchIndex`都可以通过心跳的发送和回复逐步被重建, `Leader`会根据回复信息判断出哪些 `Log`被 `commit`了。

**什么时候持久化？**

将任何数据持久化到硬盘上都是巨大的开销, 其开销远大于 `RPC`, 因此需要仔细考虑什么时候将数据持久化。

如果每次修改三个需要持久化的数据: `votedFor`, `currentTerm`, `Log`时, 都进行持久化, 其持久化的开销将会很大， 很容易想到的解决方案是进行批量化操作， 例如`<font color='cornflowerblue'>`**只在回复一个 `RPC`或者发送一个 `RPC`时，才进行持久化操作**`</font>`。

### 5.10 日志压缩、快照

- ##### 快照是什么？

  `Log`实际上是描述了某个应用的操作, 以一个 `K/V数据库`为例, `Log`就是 `Put`或者 `Get`, 当这个应用运行了相当长的时间后, 其积累的 `Log`将变得很长, 但 `K/V数据库`实际上键值对并不多, 因为 `Log`包含了大量的对同一个键的赋值或取值操作。

  因此， 应当设计一个阈值，例如1M， **`<font color='red'>`将应用程序的状态做一个快照`</font>`**，然后丢弃这个快照之前的 `Log`。

  这里有三大关键点：


  1. 快照是 `Raft`要求**上层的应用程序做的**, 因为 `Raft`本身并不理解应用程序的状态和各种命令
  2. `Raft`需要选取一个 `Log`作为快照的**分界点**, 在这个分界点要求应用程序做快照, 并**删除这个分界点之前的 `Log`**
  3. 在**持久化快照**的同时也**持久化这个分界点之后的 `Log`**。

  引入快照后, `Raft`启动时需要检查是否有之前创建的快照, 并迫使应用程序应用这个快照。

  > **总结：**
  >
  > 当在Raft协议中的日志变得太大时，为了避免无限制地增长，系统可能会采取**`快照（snapshot）`的方式来保存应用程序的状态**。快照是上层应用状态的一种**紧凑表示形式**，包含在某个特定时间点的所有必要信息，以便**在需要时能够还原整个系统状态**。
  >
- ##### 何时创建快照？

  快照通常在日志**达到一定大小时创建**。这有助于限制日志的大小，防止无限制的增长。快照也可以**在系统空闲时（没有新的日志条目被追加）创建**。
- ##### 快照的传输

  以KV数据库为上层应用为例，快照的传输主要涉及：**kv数据库与raft节点之间；不同raft节点之间。**


  - **kv数据库与raft节点之间**

    因为快照是数据库的压缩表示，因此需要**由数据库打包快照，并交给raft节点**。当快照生成之后，快照内涉及的操作会被raft节点从日志中删除（不删除就相当于有两份数据，冗余了）。
  - **不同raft节点之间**

    当leader已经把某个日志及其之前（分界点）的数据库内容变成了快照，那么当涉及这部的同步时，就只能通过快照来发送，而不需要传输日志（日志已经被删了）。
- **快照造成的 `Follower`日志缺失问题?**

  假设有一个 `Follower`的日志数组长度很短, **短于 `Leader`做出快照的分界点**, 那么这`<font color='red'>`中间缺失的 `Log`将无法通过心跳 `AppendEntries RPC`发给 `Follower`（已经删除了）`</font>`, 因此这个缺失的 `Log`将永久无法被补上。

  - **解决方案1：**
    如果 `Leader`发现有 `Follower`的 `Log`落后作快照的分界点，那么 `Leader`就不丢弃快照之前的 `Log`。缺陷在于如果一个 `Follower`落后太多(例如关机了一周), 这个 `Follower`的 `Log`长度将使 `Leader<font color='red'>`无法通过快照来减少内存消耗`</font>`。
  - **解决方案2：`Raft`采用的方案。**

    `Leader`可以丢弃 `Follower`落后作快照的分界点的 `Log`。通过一个**`<font color='cornflowerblue'>InstallSnapshot RPC`来补全丢失的 `Log</font>`**, 让 `Follower`安装快照，具体过程如下:

    1. `Follower`通过 `AppendEntries`发现自己的 `Log`更短, **强制 `Leader`回退自己的 `Log`**
    2. 回退到在某个点时，`Leader`不能再回退，因为它已经**到了自己 `Log`的起点, 更早的 `Log`已经由于快照而被丢弃**
    3. `Leader`通过 `InstallSnapshot RPC`将自己的快照发给 `Follower`，`Follower`收到快照后，将其应用到自己的状态机中，从而**更新其状态到快照的状态**。
    4. 完成后，`Leader`继续通过 `AppendEntries` RPC发送快照之后的日志条目给 `Follower`，使其日志条目和状态与 `Leader`保持一致。

## 6 具体实现

### 6.1 Raft节点类的声明

这里，我们不考虑像RPC通信方法是如何实现的，仅仅聚焦于Raft算法的实现。已知Raft算法的核心有四个部分：

- 领导者选举：`sendRequestVote`、`RequestVote`
- 日志同步：`sendAppendEntries`、`AppendEntries`
- 持久化：`Persister`
- 日志压缩（快照）

另外还有一些定时器的维护：raft向状态机定时写入（ `applierTicker` ）、心跳维护定时器（ `leaderHearBeatTicker` ）、选举超时定时器（ `electionTimeOutTicker` ）。因此，在Raft算法中，最关键的就是以上几个函数。

**首先给出对Raft节点类的一个声明：**

```cpp
// Raft节点类
class Raft : public rafrRpcProctoc::raftRpc { // 继承自使用protobuf生成的raftRpc类
public:
  void AppendEntries1(const raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *reply);    // 实现 AppendEntries RPC 方法
  void applierTicker();     // 负责周期性地将已提交的日志应用到状态机
  bool CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot);    // 条件安装快照
  void doElection();    // 发起选举
  void doHeartBeat();   // 发起心跳，只有leader才需要发起心跳

  void electionTimeOutTicker();         // 选举超时定时器
  std::vector<ApplyMsg> getApplyLogs();     // 获取应用的日志
  int getNewCommandIndex();   // 获取新命令的索引
  void getPrevLogInfo(int server, int *preIndex, int *preTerm);   // 获取前一个日志的信息
  void GetState(int *term, bool *isLeader);   // 获取当前节点的状态
  void InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest *args,    // 实现 InstallSnapshot RPC 方法
                       raftRpcProctoc::InstallSnapshotResponse *reply);
  void leaderHearBeatTicker();        // 领导者心跳定时器
  void leaderSendSnapShot(int server);    // 领导者发送快照
  void leaderUpdateCommitIndex();       // 领导者更新提交索引
  bool matchLog(int logIndex, int logTerm);   // 匹配日志
  void persist();         // 持久化当前状态
  void RequestVote(const raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *reply);   // 实现 RequestVote RPC 方法
  bool UpToDate(int index, int term);   // 检查日志是否是最新的
  int getLastLogIndex();    // 获取最后一个日志的索引
  int getLastLogTerm();     // 获取最后一个日志的任期
  void getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm);     // 获取最后一个日志的索引和任期
  int getLogTermFromLogIndex(int logIndex);       // 根据日志索引获取日志的任期
  int GetRaftStateSize();   // 获取 Raft 状态的大小
  int getSlicesIndexFromLogIndex(int logIndex);   // 根据日志索引获取切片索引

  bool sendRequestVote(int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args,     // 发送 RequestVote RPC 请求
                       std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum);
  bool sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,     // 发送 AppendEntries RPC 请求
                         std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums);

  void pushMsgToKvServer(ApplyMsg msg);     // 将消息推送到 KV 服务器
  void readPersist(std::string data);    // 读取持久化数据
  std::string persistData();  

  void Start(Op command, int *newLogIndex, int *newLogTerm, bool *isLeader);    // 开始一个新的命令

  // Snapshot the service says it has created a snapshot that has
  // all info up to and including index. this means the
  // service no longer needs the log through (and including)
  // that index. Raft should now trim its log as much as possible.
  // index代表是快照apply应用的index,而snapshot代表的是上层service传来的快照字节流，包括了Index之前的数据
  // 这个函数的目的是把安装到快照里的日志抛弃，并安装快照数据，同时更新快照下标，属于peers自身主动更新，与leader发送快照不冲突
  // 即服务层主动发起请求raft保存snapshot里面的数据，index是用来表示snapshot快照执行到了哪条命令
  void Snapshot(int index, std::string snapshot);   // 快照管理

public:
  // 重写基类（protobuf生成的raftRpc）方法,因为rpc远程调用真正调用的是这个方法
  // 序列化，反序列化等操作rpc框架都已经做完了，因此这里只需要获取值然后真正调用本地方法即可。
  void AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProctoc::AppendEntriesArgs *request,
                     ::raftRpcProctoc::AppendEntriesReply *response, ::google::protobuf::Closure *done) override;
  void InstallSnapshot(google::protobuf::RpcController *controller,
                       const ::raftRpcProctoc::InstallSnapshotRequest *request,
                       ::raftRpcProctoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done) override;
  void RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProctoc::RequestVoteArgs *request,
                   ::raftRpcProctoc::RequestVoteReply *response, ::google::protobuf::Closure *done) override;


private:
  std::mutex m_mtx;    // mutex类对象，用于加互斥锁

  // 每个raft节点都需要与其他raft节点通信，所以用一个数组保存与其他节点通信的rpc通信对象
  std::vector<std::shared_ptr<RaftRpcUtil>> m_peers; 
  
  std::shared_ptr<Persister> m_persister;   // 用于持久化 Raft 状态的对象

  int m_me;   // 当前节点的索引
  int m_currentTerm;    // 当前节点的任期号
  int m_votedFor;     // 当前节点在本任期内投票的候选人ID
  std::vector<rafrRpcProctoc::LogEntry> m_logs;  // 日志条目数组
  
  int m_commitIndex;    // 当前节点最大的已提交的日志条目索引
  int m_lastApplied;    // 已经应用到状态机的最大的日志条目索引
  std::vector<int> m_nextIndex;   // 发送给本服务器的下一个日志条目的索引
  std::vector<int> m_matchIndex;  // 本服务器已知的最大匹配日志条目的索引

  enum Status { Follower, Candidate, Leader };
  Status m_status;    // 当前节点的状态（Follower、Candidate、Leader）
  
  std::shared_ptr<LockQueue<ApplyMsg>> applyChan;     // 指向一个线程安全的日志队列，
  
  std::chrono::_V2::system_clock::time_point m_lastResetElectionTime;   // 最近一次重置选举计时器的时间
  std::chrono::_V2::system_clock::time_point m_lastResetHearBeatTime;   // 最近一次重置心跳计时器的时间
  int m_lastSnapshotIncludeIndex;     // 快照中包含的最后一个日志条目的索引
  int m_lastSnapshotIncludeTerm;      // 快照中包含的最后一个日志条目的任期号
  std::unique_ptr<monsoon::IOManager> m_ioManager = nullptr;    // 指向IO管理器，用于管理 I/O 操作
};
```

整个类中有很多函数方法和成员变量，我们只关注刚刚所提到的几个关键函数。现在逐个来分析。

### 6.2 Leader选举

**Leader选举的整体流程图：**

![选举](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\选举.png)

- **electionTimeOutTicker** ：负责查看是否该发起选举，如果该发起选举就执行doElection发起选举。
- **doElection** ：实际发起选举，构造需要发送的rpc，并多线程调用sendRequestVote处理rpc及其相应。
- **sendRequestVote** ：负责发送选举中的RPC，在发送完rpc后还需要负责接收并处理对端发送回来的响应。
- **RequestVote** ：接收别人发来的选举请求，主要检验是否要给对方投票（依据term、日志条目索引的新旧）。

### 6.3 日志复制、心跳（核心）

**日志复制和心跳的整体流程图：**

![c420e41da1361a0c55ff50fd7b45b4e0](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\日志复制心跳.png)

- **leaderHearBeatTicker** :作为定时器功能，负责查看是否该发送心跳了，如果该发起就执行doHeartBeat。
- **doHeartBeat** :实际发送心跳，判断到底是构造需要发送的rpc，并多线程调用sendRequestVote处理rpc及其相应。
- **sendAppendEntries** :负责发送日志的RPC，在发送完rpc后还需要负责接收并处理对端发送回来的响应。
- **leaderSendSnapShot** :负责发送快照的RPC，在发送完rpc后还需要负责接收并处理对端发送回来的响应。
- **AppendEntries** :Follower节点，接收leader发来的日志请求，主要检验用于检查当前日志是否匹配并同步leader的日志到本机。
- **InstallSnapshot** ::Follower节点，接收leader发来的快照请求，同步快照到本机。

#### AppendEntries

主要作用：实现了 Raft 协议中的 AppendEntries RPC，用于将日志条目从领导者（Leader）发送到跟随者（Follower）时，在Follower节点处理Leader发送的追加日志条目请求，并根据接收到的请求更新跟随者的状态和日志。

```cpp
/*
AppendEntries1：实现了 Raft 协议中的 AppendEntries RPC，用于将日志条目从领导者（Leader）发送到跟随者（Follower）
主要功能：处理领导者发送的追加日志条目请求，并根据接收到的请求更新跟随者的状态和日志
输入参数：   args
            reply
*/
void Raft::AppendEntries1(const raftRpcProctoc::AppendEntriesArgs* args, raftRpcProctoc::AppendEntriesReply* reply) {
  std::lock_guard<std::mutex> locker(m_mtx);    // RAII互斥锁
  reply->set_appstate(AppNormal);   // 表示网络正常
  // Your code here (2A, 2B).
  //	不同的人收到AppendEntries的反应是不同的，要注意无论什么时候收到rpc请求和响应都要检查term

  // 消息的任期号比当前节点的任期号小，说明是过期的消息
  if (args->term() < m_currentTerm) { 
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    // 告诉领导者（Leader），当前节点拒绝了请求，因为领导者的任期号（term）比当前节点的任期号低，领导者需要及时更新其 nextIndex。设置 updatenextindex 为 -100 是一种特殊的标志，表示这种特定情况的拒绝。
    reply->set_updatenextindex(-100);   // 让领导人可以及时更新自己
    DPrintf("[func-AppendEntries-rf{%d}] 拒绝了 因为Leader{%d}的term{%v}< rf{%d}.term{%d}\n", m_me, args->leaderid(),
            args->term(), m_me, m_currentTerm);
    return;  // 注意从过期的领导人收到消息不要重设超时定时器
  }

  DEFER { persist(); }  // 本函数结束后执行持久化。执行persist的时候应该也是处于加锁状态的（locker还未释放）
  // 如果领导者的任期号大于当前任期号，更新当前任期号，并将状态设置为跟随者。
  if (args->term() > m_currentTerm) { 
    // 三变 ,防止遗漏，无论什么时候都是三变
    m_status = Follower;
    m_currentTerm = args->term();
    m_votedFor = -1;     // 这里设置成-1有意义，如果突然宕机然后上线理论上是可以投票的
  }

  myAssert(args->term() == m_currentTerm, format("assert {args.Term == rf.currentTerm} fail")); // 断言
  m_status = Follower;    // 收到了Leader的消息，设为Follower，这里是有必要的，因为如果candidate收到同一个term的leader的AE，需要变成follower
  m_lastResetElectionTime = now();    // 重置选举计时器

  // 不能无脑的从prevlogIndex开始添加日志，因为rpc可能会延迟，导致发过来的log是很久之前的
  // 比较leader之前已经同步日志的最大索引和当前节点的最新日志索引，有三种情况：
  if (args->prelogindex() > getLastLogIndex()) {
    // 情况1：领导者的 prevLogIndex 大于当前节点的 lastLogIndex，说明当前节点没更新之前的日志。当前节点无法处理这次 AppendEntries 请求，因为它没有 prevLogIndex 所指的日志条目（leader发送的日志太新了）
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(getLastLogIndex() + 1);    // 向leader申请当前节点最后一个日志的下一个，没有用匹配加速
    return;
  } else if (args->prevlogindex() < m_lastSnapshotIncludeIndex) {
    // 情况2：领导者的 prevLogIndex 小于当前节点的 lastSnapshotIncludeIndex，说明领导者发送的日志条目已经被当前节点截断并快照化了，当前节点无法处理这些过时的日志条目。（leader发送的日志太老了）
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(m_lastSnapshotIncludeIndex + 1);
  }
  // 情况3： prevLogIndex 在当前节点的日志范围内，需要进一步输入的logIndex所对应的log的任期是不是logterm。
  // 情况3.1：prevLogIndex和prevLogTerm都匹配，还需要一个一个检查所有的当前新发送的日志匹配情况（有可能follower已经有这些新日志了）
  if (matchLog(args->prevlogindex(), args->prevlogterm())) {
    // 如果匹配。不能直接截断，必须一个一个检查，因为发送来的log可能是之前的，直接截断可能导致“取回”已经在follower日志中的条目
    // 不直接截断的处理逻辑是逐条检查和更新日志，而不是简单地从发现不匹配的位置开始删除所有后续日志条目。这是为了避免误删已经存在并且可能是正确的日志条目。
    for (int i = 0; i < args->entries_size(); i++) {
      auto log = args->entries(i);  // 遍历取出日志条目
      if (log.logindex() > getLastLogIndex()) {   //  超过follower中的最后一个日志，就直接添加日志
        m_logs.push_back(log);
      } else {
         // 没超过就说明follower已经有这些新日志了，比较是否匹配，不匹配再更新，而不是直接截断(直接截断有可能会造成丢失)
         if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() == log.logterm() &&
             m_logs[getSlicesIndexFromLogIndex(log,logindex())].command() != log.command()) {
          // term和index相等，则两个日志应该也相等（raft基本性质），这里却不符合，出现异常
          myAssert(false, format("[func-AppendEntries-rf{%d}] 两节点logIndex{%d}和term{%d}相同，但是其command{%d:%d}   "
                                 " {%d:%d}却不同！！\n",
                                 m_me, log.logindex(), log.logterm(), m_me,
                                 m_logs[getSlicesIndexFromLogIndex(log.logindex())].command(), args->leaderid(),
                                 log.command()));
         }
         if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() != log.logterm()) { // term不匹配
            m_logs[getSlicesIndexFromLogIndex(log.logindex())] = log;  // 相同的索引位置上发现日志条目的任期不同，意味着日志存在不一致（这个来自于旧的领导者，替换为新的）
         }
      }
    }
    // 验证更新后的最后一个日志索引是否正确，确保日志条目数量和索引的一致性
    myAssert(
        getLastLogIndex() >= args->prevlogindex() + args->entries_size(),   // 因为可能会收到过期的log！！！ 因此这里是大于等于
        format("[func-AppendEntries1-rf{%d}]rf.getLastLogIndex(){%d} != args.PrevLogIndex{%d}+len(args.Entries){%d}",
               m_me, getLastLogIndex(), args->prevlogindex(), args->entries_size()));
  
    // 更新提交索引，领导者的提交索引（args->leadercommit()落后于getLastLogIndex()的情况），或者是当前日志的最后一个索引（follower日志还没更新完全）
    if (args->leadercommit() > m_commitIndex) {
      m_commitIndex = std::min(args->leadercommit(), getLastLogIndex());
    }

    // l确保follower最后一个日志索引不小于follower提交索引
    myAssert(getLastLogIndex() >= m_commitIndex,
             format("[func-AppendEntries1-rf{%d}]  rf.getLastLogIndex{%d} < rf.commitIndex{%d}", m_me,
                    getLastLogIndex(), m_commitIndex));

    // 返回成功响应
    reply->set_success(true);
    reply->set_term(m_currentTerm);
    return;
  } else {   
    // 情况3.2：term不匹配
    // PrevLogIndex 长度合适，但是不匹配，因此往前寻找 矛盾的term的第一个元素
    // 为什么该term的日志都是矛盾的呢？也不一定都是矛盾的，只是这么优化减少rpc而已
    // 什么时候term会矛盾呢？很多情况，比如leader接收了日志之后马上就崩溃等等
    reply->set_updatenextindex(args->prevlogindex());   // 初始设置为prevlogindex
    // 从 prevlogindex 开始，向前查找与 prevlogterm 不同term的第一个日志条目，并更新 updatenextindex
    for (int index = args->prevlogindex(); index >= m_lastSnapshotIncludeIndex; --index) {  
    if (getLogTermFromLogIndex(index) != getLogTermFromLogIndex(args->prevlogindex())) {  // 使用了匹配加速，如果某一个日志不匹配，那么这一个日志所在的term的所有日志大概率都不匹配，直接找与他不同term的最后一个日志条目
      reply->set_updatenextindex(index + 1);
      break;
    }
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    return;
  }
  }
}
```

让我们简单分析下代码。本函数为通过RPC实现的远程调用，在Leader节点中通过RPC调用这个函数，处理所传输的日志添加请求。

**Follower节点在收到这个请求时，会对其进行处理。处理主要包括以下几个步骤：**

1. 检查Leader节点发送过来的请求消息中所携带的 `term`和Follwer节点的 `term`大小，来判断是否为过期消息。（**无论什么时候收到rpc请求和响应都要检查term**）
2. 比较请求消息中的上一个日志条目索引 `args->prevlogindex()`和当前Follower节点的最新日志索引**，`<font color='red'>`判断如何处理这次 AppendEntries 请求（不同index情况决定了是如何处理）`</font>`**。
3. 索引匹配后，还需要检查输入的 `prevLogIndex`所对应的log的任期是不是 `args->prevlogterm()`，判断日志条目是否已经过期需要更新。

这里，我们使用了**日志寻找匹配加速**的方法，具体代码：

```cpp
reply->set_updatenextindex(args->prevlogindex());   // 初始设置为prevlogindex
    // 从 prevlogindex 开始，向前查找与 prevlogterm 不同term的第一个日志条目，并更新 updatenextindex
    for (int index = args->prevlogindex(); index >= m_lastSnapshotIncludeIndex; --index) {  
        if (getLogTermFromLogIndex(index) != getLogTermFromLogIndex(args->prevlogindex())) {  // 使用了匹配加速，如果某一个日志不匹配，那么这一个日志所在的term的所有日志大概率都不匹配，直接找与他不同term的最后一个日志条目
          reply->set_updatenextindex(index + 1);
          break;
        }
    }
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    return;
```

当 `prevLogIndex`和当前节点的日志索引匹配，但是在当前节点中 `prevLogIndex`所对应的log的任期不是 `args->prevlogterm()`。按照常规的方法，此时需要一个一个地向前倒退。但这样如果前面不匹配的日志条目过多，就会导致消耗过多资源。因此这里直接寻找当前节点中与 `prevLogIndex`所对应的log的任期不同的最新日志（如果某一个日志不匹配，那么这一个日志所在的term的所有日志大概率都不匹配），这样虽然可能导致一些日志没必要重新同步，但是避免了一步一步去匹配。

整理这个函数的处理逻辑如下：

```cpp
AppendEntries1
├── 获取互斥锁并设置初始状态
├── 判断消息任期号与当前任期号
│   ├── 消息任期号 < 当前任期号
│   │   └── 拒绝请求并更新 reply
│   │       ├── 设置 success 为 false
│   │       ├── 设置 term 为当前任期号
│   │       └── 设置 updatenextindex 为 -100
│   └── 消息任期号 >= 当前任期号
│       ├── 消息任期号 > 当前任期号
│       │   ├── 更新当前任期号
│       │   ├── 更新节点状态为 Follower
│       │   └── 重置 votedFor
│       └── 消息任期号 == 当前任期号
│           ├── 更新状态为 Follower
│           └── 重置选举计时器
├── 检查 prevLogIndex 的情况
│   ├── prevLogIndex > 当前节点的 lastLogIndex
│   │   └── 拒绝请求并更新 reply
│   │       ├── 设置 success 为 false
│   │       ├── 设置 term 为当前任期号
│   │       └── 设置 updatenextindex 为 lastLogIndex + 1
│   ├── prevLogIndex < lastSnapshotIncludeIndex
│   │   └── 拒绝请求并更新 reply
│   │       ├── 设置 success 为 false
│   │       ├── 设置 term 为当前任期号
│   │       └── 设置 updatenextindex 为 lastSnapshotIncludeIndex + 1
│   └── prevLogIndex 和 prevLogTerm 匹配
│       ├── 逐条检查并更新日志条目（而不是直接截断，然后添加）
│       │   ├── 新日志条目添加到末尾
│       │   └── 检查并更新已有日志条目
│       └── 更新 commitIndex
│       └── 设置 reply
│           ├── 设置 success 为 true
│           └── 设置 term 为当前任期号
└── 否则
    └── 向前查找不匹配的日志条目并更新 reply
        ├── 设置 updatenextindex 为 prevLoIndex
        ├── 找到不同 term 的第一个日志条目
		└── 设置 reply

```

#### **sendAppendEntries**

本函数主要作用是向指定的raft节点发送附加日志条目（AppendEntries）请求，并处理该节点的回复reply来判断是否成功完成请求，以进行后续的处理。主要的处理包括后续日志提交（Commit），以及如果请求被拒绝了后寻找匹配的日志条目。

```cpp
/*
sendAppendEntries 函数
主要功能：向指定的raft节点发送附加日志条目（AppendEntries）请求，并处理该节点的回复reply来判断是否成功完成请求
输入参数：
    server: raft节点ID，发送请求给该节点。
    args: 要发送的附加日志条目请求参数。
    reply: 用于存储raft节点的回复。
    appendNums: 用于跟踪成功追加日志条目的服务器数量。
*/
bool Raft::sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                             std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply,
                             std::shared_ptr<int> appendNums) {
  // 调用目标节点的重写的AppendEntries函数接收追加日志请求
  bool ok = m_peers[server]->AppendEntries(args.get(), reply.get());  // RPC发挥远程调用的作用，注意智能指针要转换为裸指针

  // 检查网络通信
  //这个ok是网络是否正常通信的ok，而不是requestVote rpc是否投票的rpc
  // 如果网络不通的话肯定是没有返回的，不用一直重试
  // todo： paper中5.3节第一段末尾提到，如果append失败应该不断的retries ,直到这个log成功的被store
  if (!ok) {  // 通信失败
    DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc失敗", m_me, server);
    return ok;
  }

  // 通信成功
  DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc成功", m_me, server);
  
  if (reply->appstate() == Disconnected) {  // 通信成功，但是返回状态为Disconnected，但由于各种原因（如服务器重启、网络分区等）,远端 RPC 节点已经断连或不可用
    return ok;
  }

  // 节点可用，处理节点返回的回复 
  std::lock_guard<std::mutex> lg1(m_mtx);   // 加锁，保护共享资源
  if (reply->term() > m_currentTerm) {  // 对端raft节点的term比当前节点的term更新
    m_status = Follower;  // 退为Follower
    m_currentTerm = reply->term();
    m_votedFor = -1;    // 重置投票记录
    return ok;
  } else if (reply->term() < m_currentTerm) { // 对端服务节点的term比当前的小，则这个reply是过期消息，不处理
    DPrintf("[func -sendAppendEntries  rf{%d}]  节点：{%d}的term{%d}<rf{%d}的term{%d}\n", m_me, server, reply->term(),
            m_me, m_currentTerm);
    return ok;
  }

  // 上述符合，则任期相等，则需要处理reply
  if (m_status != Leader) {     // 当前节点已经退回了Folloer，则不需要其处理reply
    return ok;
  }

  myAssert(reply->term() == m_currentTerm,
           format("reply.Term{%d} != rf.currentTerm{%d}   ", reply->term(), m_currentTerm));   // 断言检查
  
  if (!reply->success()) {
    // 回复中声明这次请求没有成功，则说明日志条目的index不匹配，正常来说就是index要往前-1来寻找最后一个匹配的日志条目
    // 第一个日志（idnex = 1）发送后肯定是匹配的，因此不用考虑变成负数
    if (reply->updatenextindex() != -100) {
      DPrintf("[func -sendAppendEntries  rf{%d}]  返回的日志term相等, 但是index不匹配, 回缩nextIndex[%d]: {%d}\n", m_me,
              server, reply->updatenextindex());
      m_nextIndex[server] = reply->updatenextindex();  // 直接退回到raft节点返回来的日志条目
    }
    //	怎么越写越感觉rf.nextIndex数组是冗余的呢，看下论文fig2，其实不是冗余的
  } else {
    // 请求成功，日志条目是匹配的，新的日志条目已经添加到了对端raft节点上
    *appendNums = *appendNums + 1;  // 成功+1
    DPrintf("---------------------------tmp------------------------- 节点{%d}返回true,当前*appendNums{%d}", server, *appendNums);
    // 更新这个对端Follower节点的日志条目信息
    m_matchIndex[server] = std::max(m_matchIndex[server], args->prevlogindex() + args->entries_size()); // Follower中当前匹配的最新日志条目索引号
    m_nextIndex[server] = m_matchIndex[server] + 1;   // Follower中下一个想要日志条目的索引号

    int lastLogIndex = getLastLogIndex();   // Leader中最新的日志索引

    myAssert(m_nextIndex[server] <= lastLogIndex + 1, // 肯定不能超过最新的，否则是不合理的
             format("error msg:rf.nextIndex[%d] > lastLogIndex+1, len(rf.logs) = %d   lastLogIndex{%d} = %d", server, m_logs.size(), server, lastLogIndex));

    // 检查是否可以提交日志条目（多数follower节点成功更新）
    if (*appendNums >= 1 + m_peers.size() / 2) {
      *appendNums = 0; // 重置

      // leader只有在当前term有日志提交的时候才更新commitIndex，因为raft无法保证之前term的Index是否提交
      // 只有当前term有日志提交，之前term的log才可以被提交，只有这样才能保证“领导人完备性{当选领导人的节点拥有之前被提交的所有log，当然也可能有一些没有被提交的}”
      if (args->entries_size() > 0) {
        DPrintf("args->entries(args->entries_size()-1).logterm(){%d}   m_currentTerm{%d}",
                args->entries(args->entries_size() - 1).logterm(), m_currentTerm);
      }

      if (args->entries_size() > 0 && args->entries(args->entries_size() - 1).logterm() == m_currentTerm) {    // 保证存在日志、且存在属于当前term的日志
        DPrintf(
            "---------------------------tmp------------------------- 當前term有log成功提交, 更新leader的m_commitIndex "
            "from{%d} to{%d}",
            m_commitIndex, args->prevlogindex() + args->entries_size());
        m_commitIndex = std::max(m_commitIndex, args->prevlogindex() + args->entries_size());    // 更新leader的提交索引m_commitIndex
      }

      // 检查，提交索引不应该超过最新的日志索引
      myAssert(m_commitIndex <= lastLogIndex,
               format("[func-sendAppendEntries,rf{%d}] lastLogIndex:%d  rf.commitIndex:%d\n", m_me, lastLogIndex,
                      m_commitIndex));

      // 这里只是提交了，具体应用还没有。（后续通过应用定时器）

    }
  }
  // 返回消息处理完毕
  return ok;  
}
```

本函数中存在比较复杂的处理逻辑，主要是在收到响应消息后，根据响应消息中的term、log index来进行对应地处理。函数的整体处理逻辑如下所示：

```cpp
sendAppendEntries
├── 向目标节点发送附加日志请求
│   ├── 调用目标节点的 AppendEntries 函数
│   └── 检查网络通信是否成功
│       ├── 通信失败
│       │   └── 返回通信状态 ok
│       └── 通信成功
│           ├── 检查节点返回状态
│           │   ├── 返回状态为 Disconnected
│           │   │   └── 返回通信状态 ok
│           │   └── 节点可用，处理节点回复
│               ├── 加锁保护共享资源
│               ├── 比较回复中的任期与当前任期
│               │   ├── 回复任期 > 当前任期
│               │   │   ├── 设置当前节点为 Follower
│               │   │   ├── 更新当前任期
│               │   │   └── 重置投票记录
│               │   └── 回复任期 <= 当前任期
│               │       ├── 回复任期 < 当前任期
│               │       │   └── 忽略过期消息
│               │       └── 回复任期 == 当前任期
│               │           ├── 当前节点不是 Leader
│               │           │   └── 返回通信状态 ok
│               │           └── 当前节点是 Leader，处理回复
│               │               ├── 检查日志条目匹配状态
│               │               │   ├── 日志条目不匹配
│               │               │   │   └── 回滚 nextIndex
│               │               │   └── 日志条目匹配
│               │               │       ├── 增加成功追加日志条目数量
│               │               │       ├── 更新对端节点日志信息
│               │               │       ├── 检查是否可以提交日志条目
│               │               │       │   ├── 多数节点成功更新
│               │               │       │   │   └── 检查当前任期的日志条目
│               │               │       │   │       ├── 存在日志且属于当前任期
│               │               │       │   │       │   ├── 更新 Leader 的提交索引
│               │               │       │   │       │   └── 检查提交索引合理性
│               │               │       │   │       └── 提交索引未超过最新日志索引
│               │               │       │   └── 返回消息处理完毕
│               │               └── 返回通信状态 ok
└── 返回通信状态 ok

```

#### **InstallSnapshot**

主要作用：实现了 Raft 协议中的 InstallSnapshot RPC，用于领导者（Leader）向跟随者（Follower）发送安装快照的请求时时，在Follower节点根据快照信息，更新当前节点的状态和日志，同时将快照应用到状态机并持久化。

```cpp
/*
InstallSnapshot 函数
主要功能：处理 Leader 发送的快照数据，更新当前节点的状态和日志，同时将快照应用到状态机并持久化
输入参数：
        const raftRpcProctoc::InstallSnapshotRequest* args,   快照安装请求消息
        raftRpcProctoc::InstallSnapshotResponse* reply        快照安装结果响应消息
*/
void Raft::InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest* args,
                           raftRpcProctoc::InstallSnapshotResponse* reply) {
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };

  // 比较请求消息中的term和当前raft节点的term，如果请求消息中的term更小，则是过期的消息，拒绝快照
  if (args->term() < m_currentTerm) { 
    reply->set_term(m_currentTerm);
    return;
  }

  // 如果请求消息的term比当前节点的term更大，更新当前节点的 term 并转为 Follower
  if (args->term() > m_currentTerm) { 
    // 三变
    m_currentTerm = args->term();
    m_votedFor = -1;
    m_status = Follower;
    persist();  
  }

  m_status = Follower;
  m_lastResetElectionTime = now();  // 重置选举定时器（因为收到了来自leader的快照，不需要重新选举）

  // 比较快照的索引，如果收到的快照的索引小于等于当前节点的最后快照索引，说明是旧快照，忽略
  if (args->lastsnapshotincludeindex() <= m_lastSnapshotIncludeIndex) {
    return;
  }

  // 截断日志
  // 如果除了要生成快照的，还有更多的日志，则做一个截断，分解点之前的日志条目删除
  // 如果最大日志索引比快照要小，则直接全部删除，因为有了快照
  auto lastLogIndex = getLastLogIndex();

  if (lastLogIndex > args->lastsnapshotincludeindex()) {
    m_logs.erase(m_logs.begin(), m_logs.begin() + getSlicesIndexFromLogIndex(args->lastsnapshotincludeindex()) + 1);   // 删除快照中截止索引之前的
  } else {
    m_logs.clear();  // 本节点中所有的日志条目都在快照之前，直接全部删除
  }

  // 修改commitIndex和lastApplied，生成快照的日志条目一定是已经被应用到状态机里的
  m_commitIndex = std::max(m_commitIndex, args->lastsnapshotincludeindex());
  m_lastApplied = std::max(m_lastApplied, args->lastsnapshotincludeindex());

  m_lastSnapshotIncludeIndex = args->lastsnapshotincludeindex();
  m_lastSnapshotIncludeTerm = args->lastsnapshotincludeterm();

  // 构造响应消息
  reply->set_term(m_currentTerm);
  
  // 构造应用消息，安装快照，将快照应用到本节点上的状态机中
  ApplyMsg msg;
  msg.SnapshotValid = true;
  msg.Snapshot = args->data();
  msg.SnapshotTerm = args->lastsnapshotincludeterm();
  msg.SnapshotIndex = args->lastsnapshotincludeindex();

  // 新开一个线程，异步应用快照到 KV 服务器
  std::thread t(&Raft::pushMsgToKvServer, this, msg);
  t.detach();

  // 持久化当前状态和快照数据
  m_persister->Save(persistData(), args->data());
}
```

**Follower节点在收到这个请求时，会对其进行处理。处理主要包括以下几个步骤：**

1. 检查Leader节点发送过来的请求消息中所携带的 `term`和Follwer节点的 `term`大小，来判断是否为过期消息。（**无论什么时候收到rpc请求和响应都要检查term**）
2. 比较请求消息中快照的最新日志索引 `args->lastsnapshotincludeindex()`和当前Follower节点的最新日志索引**，`<font color='red'>`判断如何处理这次 InstallSnapshot请求（不同index情况决定了是如何处理）`</font>`**。
3. 如果是新的快照，需要应用快照到状态机（通过开辟新线程异步应用），同时持久化当前节点的状态和快照数据。

整理这个函数的处理逻辑如下：

```cpp
InstallSnapshot
├── 获取互斥锁并设置 DEFER 解锁
├── 判断请求消息任期号与当前任期号
│   ├── 请求消息任期号 < 当前任期号
│   │   └── 拒绝快照
│   │       └── 设置 reply.term 为当前任期号
│   └── 请求消息任期号 >= 当前任期号
│       ├── 请求消息任期号 > 当前任期号
│       │   ├── 更新当前任期号
│       │   ├── 设置 votedFor 为 -1
│       │   ├── 设置状态为 Follower
│       │   └── 持久化当前状态
│       ├── 设置状态为 Follower
│       └── 重置选举定时器
├── 判断快照索引
│   ├── 收到的快照索引 <= 当前节点最后快照索引
│   │   └── 忽略快照
│   └── 收到的快照索引 > 当前节点最后快照索引
│       ├── 截断日志
│       │   ├── 最大日志索引 > 快照索引
│       │   │   └── 删除快照中截止索引之前的日志
│       │   └── 最大日志索引 <= 快照索引
│       │       └── 清空所有日志
│       ├── 修改 commitIndex 和 lastApplied
│       │   ├── 更新 commitIndex
│       │   └── 更新 lastApplied
│       ├── 更新最后快照索引和任期
│       │   ├── 更新 lastSnapshotIncludeIndex
│       │   └── 更新 lastSnapshotIncludeTerm
│       └── 构造响应消息
│           ├── 设置 reply.term 为当前任期号
│           └── 构造应用消息
│               ├── 设置 SnapshotValid 为 true
│               ├── 设置 Snapshot 为 args.data
│               ├── 设置 SnapshotTerm 为 lastsnapshotincludeterm
│               └── 设置 SnapshotIndex 为 lastsnapshotincludeindex
├── 异步应用快照到状态机
│   └── 新开线程调用 pushMsgToKvServer
└── 持久化当前状态和快照数据
    └── 调用 m_persister->Save
```

#### **leaderHearBeatTicker**

此函数是作为一个心跳定时器，循环地触发Leader向Follower发送心跳来维持它的领导地位。实现该函数的基本思路是将其置为一个 `while(true)`循环中，这样可以不断地执行来检查。同时，根据发送心跳的时间间隔设置一个睡眠时间，通过让承载这个定时器的线程/协程睡眠一段时间来避免一直在循环。函数的具体代码如下：

```cpp
/*
leaderHearBeatTicker 函数
主要功能：定时器功能，在 Raft 协议的 Leader 节点中用于控制定期向Follower节点发送心跳消息
*/
void Raft::leaderHearBeatTicker() {
  while (true) {
    //不是leader的话就没有必要进行后续操作，况且还要拿锁，很影响性能，目前是睡眠，后面再优化优化
    while (m_status != Leader) {
      usleep(1000 * HeartBeatTimeout);   // 如果不是 Leader，则休眠一段时间（HeartBeatTimeout 毫秒），然后重新检查
    }
    static std::atomic<int32_t> atomicCount = 0;    // 统计Leader节点的心跳定时器的执行次数

    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};   // 记录合适的睡眠时间
    std::chrono::system_clock::time_point wakeTime{};    // 记录当前时间
    {  
      std::lock_guard<std::mutex> lock(m_mtx);   // 加锁
      wakeTime = now();
      // 睡眠时间 = 心跳超时时间 HeartBeatTimeout + 最后一次重置心跳时间 m_lastResetHearBeatTime - 当前时间
      suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout) + m_lastResetHearBeatTime - wakeTime;   
    }  // 出了这个域，已自动解锁

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) {  // 适合睡眠时间大于 1 毫秒，则执行睡眠操作
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                << std::endl;
      // 获取当前时间点
      auto start = std::chrono::steady_clock::now();    // 稳态时钟，在测量时间间隔时特别有用，因为它不会受到系统时间调整的影响，确保时间间隔测量的准确性和稳定性
      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());     //  钩子函数，让出协程的执行权

      // 获取睡眠结束后的时间点
      auto end = std::chrono::steady_clock::now();

      // 计算时间差并输出结果（单位为毫秒）
      std::chrono::duration<double, std::milli> duration = end - start;

      // 使用ANSI控制序列将输出颜色修改为紫色
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m" << std::endl;
      ++atomicCount;
    }

    // 睡眠结束
    if (std::chrono::duration<double, std::milli>(m_lastResetHearBeatTime - wakeTime).count() > 0) {
      // 如果在睡眠的这段时间定时器被重置了，则再次睡眠
      continue;
    }

    // 执行心跳
    doHeartBeat();
  }
}
```

在这里，该函数只适用对于Leader节点（只有Leader才会发心跳），我们首先计算出了应该睡眠的时间 `suitableSleepTime`，然后**通过钩子函数 `usleep`让此协程让出指定时间执行权**。`usleep`是我们实现的一个钩子函数，这是因为**我们使用协程来管理任务调度**，因此在这里的睡眠不应该调用系统函数来让出线程执行权，而是让出协程执行权，并通过定时器使得在 `suitableSleepTime`时间后这个函数还会获得执执行权。

在睡眠结束后，此时Leader应该向**所有其他节点发送心跳消息** `doHeartBeat`。执行完成后又**进入下一次循环睡眠**，以此实现了一个”定时器“的功能，在一定时间间隔不断发送心跳。

#### **doHeartBeat**

本函数为**执行心跳函数或者是日志复制的主要发起函数**。当 `leaderHearBeatTicker`定时器触发后，会调用此函数。此函数会对对Follower（除了自己外的所有节点）发送日志消息。首先，此函数会根据所记录的每个Follower的日志索引情况来判断是发送快照还是AE，然后再调用具体的函数（如leaderSendSnapShot或者sendAppendEntries去实际执行）。

```cpp
/*
doHeartBeat 函数
主要功能：Raft协议中Leader节点的心跳发送函数
*/
void Raft::doHeartBeat() {
  std::lock_guard<std::mutex> g(m_mtx);  // 锁定互斥量以确保线程安全

  // 只有leader才需要发送心跳
  if (m_status == Leader) {
    DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了且拿到mutex, 开始发送AE\n", m_me);
    auto appendNums = std::make_shared<int>(1);   // 统计正确返回的节点数量

    // 对Follower（除了自己外的所有节点）发送AE
    // todo 这里肯定是要修改的，最好使用一个单独的goruntime来负责管理发送log，因为后面的log发送涉及优化之类的
    // 最少要单独写一个函数来管理，而不是在这一坨
    for (int i = 0; i < m_peers.size(); i++) {
      if (i == m_me) {
        continue;
      }
      DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了 index:{%d}\n", m_me, i);
      myAssert(m_nextIndex[i] >= 1, format("rf.nextIndex[%d] = {%d}", i, m_nextIndex[i]));  // 确保发送给Follower的日志索引在正常范围

      // 判断是发送AE（AppendEntries）还是快照
      if (m_nextIndex[i] <= m_lastSnapshotIncludeIndex) { // 需要发送的日志条目已经删除了，因为形成了快照，所要发送快照
        std::thread t(&Raft::leaderSendSnapShot, this, i);   // 创建新线程执行发送快照函数
        t.detach();
        continue;
      }

      // 发送AE
      int preLogIndex = -1;
      int preLogTerm = -1;
      getPrevLogInfo(i, &preLogIndex, &preLogTerm); // 获取需要向第i个Follower发送的日志条目的上一个日志条目的索引和任期
 
      // 构造AE请求参数 AppendEntriesArgs
      std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> appendEntriesArgs = std::make_shared<raftRpcProctoc::AppendEntriesArgs>(); 
      appendEntriesArgs->set_term(m_currentTerm);
      appendEntriesArgs->set_leaderid(m_me);
      appendEntriesArgs->set_prevlogindex(preLogIndex);
      appendEntriesArgs->set_prevlogterm(preLogTerm);
      appendEntriesArgs->clear_entries();
      appendEntriesArgs->set_leadercommit(m_commitIndex);

      // 添加日志条目
      if (preLogIndex != m_lastSnapshotIncludeIndex) {  
        // 前一个日志条目在快照之外，应该从该索引之后开始发送日志条目
        for (int j = getSlicesIndexFromLogIndex(preLogIndex) + 1; j < m_logs.size(); ++j) {
          raftRpcProctoc::LogEntry* sendEntryPtr = appendEntriesArgs->add_entries();  // 返回一个指向新添加的日志条目的指针
          *sendEntryPtr = m_logs[j];  // 将该指针指向日志条目，则完成这个条目的添加  
        }
      } else {  // 即 preLogIndex == m_lastSnapshotIncludeIndex
        // 前一个日志条目正好是快照的分界点，那可以把Leader的所以日志条目都加入
        for (const auto& item: m_logs) {
          raftRpcProctoc::LogEntry* sendEntryPtr = appendEntriesArgs->add_entries();
          *sendEntryPtr = item; // =是可以点进去的，可以点进去看下protobuf如何重写这个赋值运算符的，实现直接将一个 protobuf 消息对象赋值给另一个消息对象
        }
      }

      // 检查：leader对每个节点发送的日志长短不一（每个follower的情况不同），但是都保证从prevIndex发送直到最后
      int lastLogIndex = getLastLogIndex();
      myAssert(appendEntriesArgs->prevlogindex() + appendEntriesArgs->entries_size() == lastLogIndex, format("appendEntriesArgs.PrevLogIndex{%d}+len(appendEntriesArgs.Entries){%d} != lastLogIndex{%d}", appendEntriesArgs->prevlogindex(), appendEntriesArgs->entries_size(), lastLogIndex));

      // 构造 AppendEntries 响应参数
      const std::shared_ptr<raftRpcProctoc::AppendEntriesReply> appendEntriesReply = std::make_shared<raftRpcProctoc::AppendEntriesReply>();
      appendEntriesReply->set_appstate(Disconnected);

      // 创建新线程，利用sendAppendEntries函数在新线程中接收并处理AppendEntries请求
      std::thread t(&Raft::sendAppendEntries, this, i, appendEntriesArgs, appendEntriesReply, appendNums);
      t.detach();
    }
    m_lastResetHearBeatTime = now();   // 更新上一次心跳时间
  }
}
```

由于一般会有多个Follower节点要发送，所以每个发送函数都是异步执行的（开辟一个新线程）。本函数的主要操作逻辑：

```cpp
doHeartBeat
├── 锁定互斥量，确保线程安全
├── 检查当前节点状态
│   └── 如果是 Leader
│       ├── 记录心跳触发日志
│       ├── 初始化成功返回的节点数量计数器
│       └── 向所有 Follower 发送 AppendEntries（AE）请求
│           ├── 遍历所有节点
│           │   ├── 跳过自身节点
│           │   └── 处理每个 Follower
│           │       ├── 确保日志索引正常
│           │       ├── 判断发送 AE 或快照
│           │       │   ├── 如果需要发送快照
│           │       │   │   ├── 创建新线程执行发送快照函数
│           │       │   │   └── 跳过当前循环
│           │       │   └── 如果发送 AE
│           │       │       ├── 获取上一个日志条目索引和任期
│           │       │       ├── 构造 AE 请求参数
│           │       │       │   ├── 设置请求参数的基础信息
│           │       │       │   ├── 清空 entries 字段
│           │       │       │   ├── 设置 leaderCommit 索引
│           │       │       │   ├── 添加日志条目
│           │       │       │   │   ├── 前一个日志条目在快照之外
│           │       │       │   │   │   └── 从该索引之后开始发送日志条目
│           │       │       │   │   └── 前一个日志条目正好是快照的分界点
│           │       │       │   │       └── 将所有日志条目都加入
│           │       │       │   ├── 检查 AE 请求的完整性
│           │       │       │   └── 记录心跳触发日志
│           │       │       ├── 构造 AE 响应参数
│           │       │       ├── 创建新线程执行 sendAppendEntries 函数
│           │       │       └── 跳过当前循环
│           └── 更新上一次心跳时间
```

#### **总结**

在日志复制和心跳这个模块中，核心内容包括：

- 整体的流程图，日志同步的过程是怎么实现的，每个函数的作用，它们之间的关系。
- 消息分为两种：AE和快照，一个是添加日志，一个是把状态机内容压缩成一个快照（则可以把快照之前的所有日志全部删除），只需要传递这个快照
- ==**最核心的内容：**每次请求和响应都需要判断 `term`和 `log index`，来判断消息是否过期，日志是否匹配，如何回退寻找匹配日志条目等==

### 6.4 持久化

Raft算法定期将一些数据持久化到磁盘中，以实现在服务器重启时利用持久化存储的数据恢复节点上一个工作时刻的状态。在这里，**我们的实现方案是将raft节点的状态信息序列化为字符串，然后将字符串写入文件中**。在这个过程中，主要依赖于**`boost`序列化库，**通过该库中所提供的序列化和反序列化函数，来实现将状态信息转化为字符串，或者从字符串中解析出raft节点信息。

利用 `boost`序列化库将当前 Raft 节点的状态序列化为字符串或从持久化字符串数据中恢复 Raft 节点的状态：

```cpp
/*
persistData 函数
主要作用：利用 Boost 序列化库将当前 Raft 节点的状态序列化为字符串
*/
std::string Raft::persistData() {
  // 创建一个用于持久化的结构体对象
  BoostPersistRaftNode boostPersistRaftNode;

  // 将当前 Raft 节点的状态保存到该对象中
  boostPersistRaftNode.m_currentTerm = m_currentTerm;  // 当前任期
  boostPersistRaftNode.m_votedFor = m_votedFor;        // 投票给的候选人
  boostPersistRaftNode.m_lastSnapshotIncludeIndex = m_lastSnapshotIncludeIndex;  // 上次快照包含的日志索引
  boostPersistRaftNode.m_lastSnapshotIncludeTerm = m_lastSnapshotIncludeTerm;    // 上次快照包含的任期

  // 将日志条目序列化并保存到对象中
  for (auto& item : m_logs) {
    boostPersistRaftNode.m_logs.push_back(item.SerializeAsString());
  }

  // 使用 stringstream 和 Boost 序列化库将对象序列化为字符串
  std::stringstream ss;
  boost::archive::text_oarchive oa(ss);
  oa << boostPersistRaftNode;

  // 返回序列化后的字符串
  return ss.str();
}


/*
readPersist 函数
主要功能：基于boost序列化库，从持久化字符串数据中恢复 Raft 节点的状态
*/
void Raft::readPersist(std::string data) {
  if (data.empty()) {
    return;
  }
  // 初始化数据流
  std::stringstream iss(data);

  // Boost 序列化对象创建
  boost::archive::text_iarchive ia(iss);

  BoostPersistRaftNode boostPersistRaftNode;  // 创建一个 BoostPersistRaftNode 对象 boostPersistRaftNode，用于存储从 ia 中反序列化得到的数据。
  ia >> boostPersistRaftNode;   // 将输入流 ia 中的数据反序列化到 boostPersistRaftNode 对象中。

  // 恢复节点状态
  m_currentTerm = boostPersistRaftNode.m_currentTerm;
  m_votedFor = boostPersistRaftNode.m_votedFor;
  m_lastSnapshotIncludeIndex = boostPersistRaftNode.m_lastSnapshotIncludeIndex;
  m_lastSnapshotIncludeTerm = boostPersistRaftNode.m_lastSnapshotIncludeTerm;

  // 恢复日志列表
  m_logs.clear();
  for (auto& item : boostPersistRaftNode.m_logs) {
    raftRpcProctoc::LogEntry logEntry;
    logEntry.ParseFromString(item);   // item被序列化成了字符串，再解析回来
    m_logs.emplace_back(logEntry);
  }
}
```

将字符串写入文件主要依赖于文件输入输出流 `fstream`，我们封装了一个 `Persister`类，其中基于 `fstream`实现了维护文件输入输出流、将字符串写入到文件等函数，以实现持久化。

## 7. Raft算法是如何实现线性一致性的？

线性一致性保证每个操作都看起来像是在某个瞬间发生，并且所有操作按照这个瞬间的顺序执行。即要求读操作必须返回最近的一次写操作给客户端。

因此，Raft算法主要通过领导者选举、日志同步、一致性检查等方式来确保线性一致性。具体来说，Raft节点集群会通过投票来选举一个Leader，**所有的请求操作都是由Leader完成的，**在处理客户端请求消息前，Leader需要将请求生成为具有递增索引日志条目，在其他Raft节点上同步，经过**多数节点同意之后才会去执行请求操作**。同时，每个节点（包括领导者和跟随者）将已提交的日志条目应用到它们的状态机，确保所有节点的状态一致。

在上述过程中，Leader和其他Raft节点之间通过日志索引和任期号，确保日志条目在所有节点上的顺序一致。

因此，所有请求的应用是按照顺序的，那么就可以保证：==**领导者在处理读取请求时，确保所有先前的写入请求已经完成并被应用。**==同时，**==所有请求的执行顺序与全局时钟中的请求时间是一致的==**。

## 参考文献

[什么是分布式系统，这么讲不信你不会 - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/99932839)

[什么是分布式，分布式和集群的区别又是什么？这一篇让你彻底明白！_什么叫分布式-CSDN博客](https://blog.csdn.net/weixin_42046751/article/details/109510811)

[终于有人把分布式系统架构讲明白了-CSDN博客](https://blog.csdn.net/hzbooks/article/details/120916132)

[MIT6.824 Lab2 RAFT 介绍与实现 - pxlsdz - 博客园 (cnblogs.com)](https://www.cnblogs.com/pxlsdz/p/15557635.html)

[MIT6.5840(6.824) Lec06笔记: raft论文解读2: 恢复、持久化和快照 (gfx9.github.io)](https://gfx9.github.io/2024/01/12/MIT6.5840/Lec06笔记/)

[分布式一致性算法-Raft_分布式一致性算法raft-CSDN博客](https://blog.csdn.net/kiranet/article/details/121130250?spm=1001.2101.3001.6661.1&utm_medium=distribute.pc_relevant_t0.none-task-blog-2~default~BlogOpenSearchComplete~Rate-1-121130250-blog-140291635.235^v43^pc_blog_bottom_relevance_base5&depth_1-utm_source=distribute.pc_relevant_t0.none-task-blog-2~default~BlogOpenSearchComplete~Rate-1-121130250-blog-140291635.235^v43^pc_blog_bottom_relevance_base5&utm_relevant_index=1)

**代码参考来源**：[youngyangyang04/KVstorageBaseRaft-cpp: 【代码随想录知识星球】项目分享-基于Raft的k-v存储数据库🔥 (github.com)](https://github.com/youngyangyang04/KVstorageBaseRaft-cpp)
