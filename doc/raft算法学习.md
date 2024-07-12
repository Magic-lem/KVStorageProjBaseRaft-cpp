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

## 2 CAP理论

- **C consistency 一致性**

- 在分布式系统中有多个节点，整个系统对外提供的服务应该是一致的。即用户**在不同的系统节点访问数据的时候应该是同样的结果**，不能出现1号节点是结果1， 2号节点是结果2这种不一致的情况。

- **A availability 可用性**

  分布式系统为用户提供服务，需要保证能够**在一些节点异常的情况下仍然支持为用户提供服务**。

- **P partition tolerance 分区容错性（容灾）**

- 分布式系统的部署可能跨省，甚至跨国。不同省份或者国家之间的服务器节点是通过网络进行连接，此时如果**两个地域之间的网络连接断开，整个分布式系统的体现就是分区容错性了**。在这种系统出现网络分区的情况下系统的服务就**需要在一致性 和 可用性之间进行取舍**。要么保持一致性，返回错误。要么是保证可用性，使得两个地域之间的分布式系统不一致。

![image (4)](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\CAP.png)

==**CAP理论一定是无法全部满足三者，只能满足其中的两者（CA、CP、AP）。**==

对于一个分布式系统而言，**<font color='red'>P是分布式的前提，必须保证</font>，因为只要有网络交互就一定会有延迟和数据丢失，这种状况我们必须接受，必须保证系统不能挂掉**。所以只剩下C、A可以选择。要么保证数据一致性（保证数据绝对正确），要么保证可用性（保证系统不出错）。

当选择了C（一致性）时，如果由于网络分区而无法保证特定信息是最新的，则系统将<font color='cornflowerblue'>**返回错误或超时**</font>。

当选择了A（可用性）时，系统将始终处理客户端的查询并尝试返回**最新的<font color='cornflowerblue'>可用的信息版本</font>**，即使由于网络分区而**无法保证其是最新的**。

## 3 分布一致性

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

## 4 强一致性（共识）算法

**共识需要满足的性质：**

- <font color='cornflowerblue'>**在非拜占庭条件下保证共识的一致性（C）**</font>。非拜占庭条件就是可信的网络条件，与你通信的节点的信息都是真实的，不存在欺骗。
- <font color='cornflowerblue'>**在多数节点存活时，保持可用性（A）**</font>。“多数”永远指的是配置文件中所有节点的多数，而不是存活节点的多数。多数等同于超过半数的节点，多数这个概念概念很重要，贯穿Raft算法的多个步骤。
- <font color='cornflowerblue'>**不依赖于绝对时间**</font>。理解这点要明白共识算法是要应对节点出现故障的情况，在这样的环境中网络报文也很可能会受到干扰从而延迟，如果完全依靠于绝对时间，会带来问题，Raft用自定的**Term（任期）作为逻辑时钟来代替绝对时间。**
- <font color='cornflowerblue'>**在多数节点一致后就返回结果**</font>，而不会受到个别慢节点的影响。这点与第二点联合理解，只要**“大多数节点同意该操作”就代表整个集群同意该操作**。对于raft来说，**”操作“是储存到日志log中**，一个操作就是log中的一个entry。

==**常见的强一致性算法：**主从同步、多数派、Paxos、Raft、ZAB==





## 5 Raft算法 

### 5.1 算法概述

![image (6)](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\raft概述.png)

Raft算法又被称为基于**日志复制**的一致性算法，旨在解决分布式系统中多个节点之间的数据一致性问题。它通过选举一个**`领导者（Leader）`**，让`领导者`负责管理和协调日志复制，确保所有节点的数据一致。

- **复制日志**

  在分布式系统中，每个节点都维护着一份日志，记录系统操作的历史。为了保证数据一致性，这些日志需要在所有节点之间保持同步。	Raft通过<font color='cornflowerblue'>**领导者选举和日志复制机制**</font>，确保所有节点的日志最终是一致的。

- **心跳机制与选举**

  Raft使用<font color='cornflowerblue'>**心跳机制**</font>来触发选举。当系统启动时，每个节点（Server）的初始状态都是追随者（Follower）。每个Server都有一个定时器，超时时间为选举超时（Election Timeout），一般为150-300毫秒。如果一个Server在超时时间内没有收到来自领导者或候选者的任何消息，定时器会重启，并开始一次选举。

- **选举过程（投票）**

  当一个追随者节点发现自己<font color='cornflowerblue'>**超过选举超时没有收到领导者的消息，就会变为候选者（Candidate），并开始新一轮选举**</font>。候选者节点会增加自己的任期号，并向其他节点发送选票请求。每个节点只能在一个任期内投一票，并且通常会**将票投给第一个请求投票的候选者**。如果一个候选人在收到足够多的选票后，就成为新的领导者。

- **多个候选者（选举失败—>重来）**

  在选举过程中，可能会出现多个候选者同时竞争领导者的位置。这时，如果某个候选者无法在选举超时前获得大多数节点的支持，**选举就会失败**。失败后，所有候选者会<font color='cornflowerblue'>**重置自己的定时器，并在下一轮超时后再次发起选举**</font>，直到选出新的领导者为止。

### 5.2 工作模式：Leader-Follower 模式

- <font color='red'>`Leader` </font>一个集群只有一个`leader`，不断地向集群其它节点发号施令(心跳、日志同步)，其它节点接到领导者日志请求后成为其追随者
- <font color='red'>`Follower`</font> 一个服从`leader`决定的角色，追随领导者，接收领导者日志，并实时同步
- <font color='red'>`Cadidate` </font>当`follower`发现集群没有`leader`，会重新选举`leader`，参与选举的节点会变成`candidate`

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

日志中保存<font color='cornflowerblue'>**客户端发送来的命令**</font>，上层的状态机根据日志执行命令，那么**日志一致，自然上层的状态机就是一致的**。结构如下：

![img](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\日志结构.png)

其中，每一个元素被称为一个日志条目`entry`，每个日志条目包含一个Index、Term以及具体的命令Command。

**核心思想**：Raft算法可以让多个节点的上层状态机保持一致的关键是让==**各个节点的日志保持一致**== **。**

### 5.5 任期 term

- **每个节点都有自己的term，Term用连续的数字进行表示**。Term会在follower发起选举（成为Candidate从而试图成为Leader ）的时候**加1**，对于一次选举可能存在两种结果：
  - 胜利当选：超过半数的节点认为当前`Candidate`有资格成为`Leader`，即超过半数的节点给当前`Candidate`投了选票。
  - 失败：如果没有任何`Candidate`（一个Term的`Leader`只有一位，但是如果多个节点同时发起选举，那么某个Term的Candidate可能有多位）获得超半数的选票，那么选举超时之后又会**开始另一个Term（Term递增）的选举**。

- 对于节点，当发现**自己的Term小于其他节点的Term时，这意味着“自己已经过期”**，不同身份的节点的处理方式有所不同：
  -  leader、Candidate：退回follower并更新term到较大的那个Term
  -  follower：更新Term信息到较大的那个Term
- > 这里解释一下为什么 自己的Term小于其他节点的Term时leader、Candidate会退回follower 而不是延续身份，因为通过Term信息知道自己过期，意味着自己可能发生了网络隔离等故障，那么在此期间整个Raft集群可能已经有了新的leader、 **提交了新的日志** ，此时自己的日志是有缺失的，如果不退回follower，那么可能会导致整个集群的日志缺失，不符合安全性。

-  相反，如果发现自己的Term大于其他节点的Term，那么就会**忽略这个消息**中携带的其他信息（**这个消息是过期的**）。

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
  2. 携带日志`entry`及其辅助信息，以控制日志的同步和日志向状态机提交
  3. 通告`leader`的index和term等关键信息以便`follower`对比确认`follower`自己或者`leader`是否过期

- ##### **follower知道leader出现故障后如何选举出leader？**

  系统中的`追随者(follower)`**未收到日志同步(也可理解为心跳)**转变成为`候选者(Candidate)`，在成为`候选者(Candidate)`后，不满足于自己现在状态，迫切的想要成为`领导者(leader)`，虽然它给自己投了1票，但很显然1票是不够，它需要其它节点的选票才能成为领导者，所以**通过RPC请求其它节点给自己投票**。

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
    >  保证第一点：仅有 leader 可以生成 entry
    >
    >  保证第二点：leader 在通过 AppendEntriesRPC 和 follower 通讯时，除了带上自己的term等信息外，还会带上entry的index和对应的term等信息，follower在接收到后通过对比就可以知道自己与leader的日志是否匹配，不匹配则拒绝请求。leader发现follower拒绝后就知道entry不匹配，那么下一次就会尝试匹配前一个entry，直到遇到一个entry匹配，并将不匹配的entry给删除（覆盖）。

    >  注意：raft为了避免出现一致性问题，要求 leader 绝不会提交过去的 term 的 entry （即使该 entry 已经被复制到了多数节点上）。leader 永远只提交当前 term 的 entry， 过去的 entry 只会随着当前的 entry 被一并提交。

- ##### raft日志同步方式

  先**找到日志不匹配的那个点，然后<font color='cornflowerblue'>只同步那个点之后的日志</font>**。

  一个`follower`，如果`leader`认为其日志已经和自己匹配了，那么在AppendEntryRPC中不用携带日志（其他信息依然要携带），反之如果follower的日志只有部分匹配，那么就需要在AppendEntryRPC中携带对应的日志。**心跳RPC可以看成是没有携带日志的特殊的日志同步RPC。**

   **日志同步（复制）的过程：**
  
  ![日志复制](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\日志复制.png)

**-------------------------------------------------------------------------------------------Raft算法的核心步骤👇------------------------------------------------------------------------------------------------**

1. 接收到客户端请求之后，`领导者`会**根据用户指令和自身任期以及日志索引等信息生成一条`新的日志项`**，并附加到本地日志中。
2. 领导者通过日志复制 RPC，将日志复制到其他跟随者节点。
3. 跟随者将日志附加到本地日志成功之后，便返回 success，此时新的日志项还没有被跟随者提交。
4. 当领导者接收到**大多数（超过集群数量的一半）**跟随者节点的 success 响应之后，便将此日志`提交(commit)`到它的状态机中。
5. 领导者将执行的结果返回给客户端。
6. 当跟随者收到<font color='red'>**下一次领导者的心跳请求或者新的日志复制请求之后**</font>，如果发现领导者已经应用了之前的日志，但是它自己还没有之后，那么它便会把这条日志项应用到本地状态机中。（类似于**二阶段提交**的方式）

**-------------------------------------------------------------------------------------------Raft算法的核心步骤👆------------------------------------------------------------------------------------------------**

- ##### 安全性保障

  `Election Safety`：每个 `term` 最多只会有一个 `leader`

  `Leader Append-Only`：`Leader`绝不会覆盖或删除自己的日志，**只会追加**

  `Log Matching`：如果两个日志的 `index` 和 `term` 相同，那么这两个日志相同  —— `raft日志的特点1`

  ​							   如果两个日志相同，那么他们之前的日志均相同 —— `raft日志的特点2`

  `Leader Completeness`：一旦一个操作被提交了，那么在之后的 `term` 中，该操作都会存在于日志中

  `State Machine Safety`：状态机一致性，一旦一个节点应用了某个 `index` 的 entry 到状态机，那么其他所有节点应用的该 `index` 的操作都是一致的（各个节点之间一致，使得上层状态机都一致）

- **为什么不直接让follower拷贝leader的日志|leader发送全部的日志给follower？** 

  会携带大量的无效的日志（因为这些日志follower本身就有）

- ##### leader如何知道follower的日志是否与自己完全匹配？

  在AppendEntryRPC中携带上entry的`index`和对应的`term`（日志的term），可以**通过比较最后一个日志的`index`和`term`**来得出某个follower日志是否匹配。

- ##### 如果发现不匹配，那么如何知道哪部分日志是匹配的，哪部分日志是不匹配的呢？

  `leader`每次发送AppendEntryRPC后，`follower`都会根据其`entry`的`index`和对应的`term`来判断某一个日志是否匹配。在leader刚当选，**会从最后一个日志开始判断是否匹配**，如果匹配，那么后续发送AppendEntryRPC就不需要携带日志entry了。

  如果不匹配，那么下一次就发送 **倒数第2个** 日志entry的index和其对应的term来判断匹配，

  如果还不匹配，那么依旧重复这个过程，即发送 倒数第3个 日志entry的相关信息

  **重复这个过程，直到遇到一个匹配的日志。**

- ##### 优化：寻找匹配加速（可选）

   在寻找匹配日志的过程中，在最后一个日志不匹配的话就尝试倒数第二个，然后不匹配继续倒数第三个。。。

   `leader和follower` 日志存在大量不匹配的时候这样会太慢，可以用一些方式一次性的多倒退几个日志，就算回退稍微多了几个也不会太影响，具体实现参考 [7.3 快速恢复（Fast Backup） - MIT6.824 (gitbook.io)](http://gitbook.io/)

### 5.9 持久化

Raft的一大优势就是**Fault Tolerance**（容灾），即能够在部分节点宕机、失联或者出现网络分区的情况下依旧让系统正常运行。为了保证这一点，除了领导选举与日志复制外，还需要定期将一些数据持久化到磁盘中，以实现在服务器重启时利用持久化存储的数据恢复节点上一个工作时刻的状态。持久化的内容仅仅是`Raft`层, 其应用层不做要求。

![img](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\持久化.png)

**论文中提到需要持久化的数据包括:**

- `voteFor`：

  `voteFor`记录了一个节点在某个`term`内的投票记录, 因此如果**不将这个数据持久化, 可能会导致如下情况**:

  - 在一个`Term`内某个节点向某个`Candidate`投票, 随后故障
  - 故障重启后, 又收到了另一个`RequestVote RPC`, 由于其<font color='red'>没有将`votedFor`持久化, 因此其不知道自己已经投过票, 结果是再次投票</font>, 这将导致同一个`Term`可能出现2个`Leader`。

- `currentTerm`:
  `currentTerm`的作用也是实现一个任期内最多只有一个`Leader`, 因为如果一个节点重启后不知道现在的`Term`时多少, 其<font color='red'>无法再进行投票时将`currentTerm`递增到正确的值</font>, 也可能导致有多个`Leader`在同一个`Term`中出现

- `Log`:
  这个很好理解, 需要用`Log`来**恢复自身的状态**

**为什么只需要持久化`votedFor`, `currentTerm`, `Log`？**

其他的数据， 包括 `commitIndex`、`lastApplied`、`nextIndex`、`matchIndex`都可以通过心跳的发送和回复逐步被重建, `Leader`会根据回复信息判断出哪些`Log`被`commit`了。

**什么时候持久化？**

将任何数据持久化到硬盘上都是巨大的开销, 其开销远大于`RPC`, 因此需要仔细考虑什么时候将数据持久化。

如果每次修改三个需要持久化的数据: `votedFor`, `currentTerm`, `Log`时, 都进行持久化, 其持久化的开销将会很大， 很容易想到的解决方案是进行批量化操作， 例如<font color='cornflowerblue'>**只在回复一个`RPC`或者发送一个`RPC`时，才进行持久化操作**</font>。

### 5.10 日志压缩、快照

- ##### 快照是什么？

  `Log`实际上是描述了某个应用的操作, 以一个`K/V数据库`为例, `Log`就是`Put`或者`Get`, 当这个应用运行了相当长的时间后, 其积累的`Log`将变得很长, 但`K/V数据库`实际上键值对并不多, 因为`Log`包含了大量的对同一个键的赋值或取值操作。

  因此， 应当设计一个阈值，例如1M， **<font color='red'>将应用程序的状态做一个快照</font>**，然后丢弃这个快照之前的`Log`。

  这里有三大关键点：

  1. 快照是`Raft`要求**上层的应用程序做的**, 因为`Raft`本身并不理解应用程序的状态和各种命令
  2. `Raft`需要选取一个`Log`作为快照的**分界点**, 在这个分界点要求应用程序做快照, 并**删除这个分界点之前的`Log`**
  3. 在**持久化快照**的同时也**持久化这个分界点之后的`Log`**。

  

  引入快照后, `Raft`启动时需要检查是否有之前创建的快照, 并迫使应用程序应用这个快照。

  > **总结：**
  >
  > 当在Raft协议中的日志变得太大时，为了避免无限制地增长，系统可能会采取**`快照（snapshot）`的方式来保存应用程序的状态**。快照是上层应用状态的一种**紧凑表示形式**，包含在某个特定时间点的所有必要信息，以便**在需要时能够还原整个系统状态**。

- ##### 何时创建快照？

   快照通常在日志**达到一定大小时创建**。这有助于限制日志的大小，防止无限制的增长。快照也可以**在系统空闲时（没有新的日志条目被追加）创建**。

- ##### 快照的传输

  以KV数据库为上层应用为例，快照的传输主要涉及：**kv数据库与raft节点之间；不同raft节点之间。**
  - **kv数据库与raft节点之间**

    因为快照是数据库的压缩表示，因此需要**由数据库打包快照，并交给raft节点**。当快照生成之后，快照内涉及的操作会被raft节点从日志中删除（不删除就相当于有两份数据，冗余了）。

  - **不同raft节点之间**

    当leader已经把某个日志及其之前（分界点）的数据库内容变成了快照，那么当涉及这部的同步时，就只能通过快照来发送，而不需要传输日志（日志已经被删了）。

- **快照造成的`Follower`日志缺失问题?**

  假设有一个`Follower`的日志数组长度很短, **短于`Leader`做出快照的分界点**, 那么这<font color='red'>中间缺失的`Log`将无法通过心跳`AppendEntries RPC`发给`Follower`（已经删除了）</font>, 因此这个缺失的`Log`将永久无法被补上。

  - **解决方案1：**
    如果`Leader`发现有`Follower`的`Log`落后作快照的分界点，那么`Leader`就不丢弃快照之前的`Log`。缺陷在于如果一个`Follower`落后太多(例如关机了一周), 这个`Follower`的`Log`长度将使`Leader`<font color='red'>无法通过快照来减少内存消耗</font>。

  - **解决方案2：`Raft`采用的方案。**

    `Leader`可以丢弃`Follower`落后作快照的分界点的`Log`。通过一个**<font color='cornflowerblue'>`InstallSnapshot RPC`来补全丢失的`Log`</font>**, 让`Follower`安装快照，具体过程如下:

    1. `Follower`通过`AppendEntries`发现自己的`Log`更短, **强制`Leader`回退自己的`Log`**
    2. 回退到在某个点时，`Leader`不能再回退，因为它已经**到了自己`Log`的起点, 更早的`Log`已经由于快照而被丢弃**
    3. `Leader`通过`InstallSnapshot RPC`将自己的快照发给`Follower`，`Follower`收到快照后，将其应用到自己的状态机中，从而**更新其状态到快照的状态**。
    4. 完成后，`Leader`继续通过`AppendEntries` RPC发送快照之后的日志条目给`Follower`，使其日志条目和状态与`Leader`保持一致。

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
- **RequestVote** ：接收别人发来的选举请求，主要检验是否要给对方投票。



### 6.3 日志复制、心跳

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
  // 情况3： prevLogIndex 在当前节点的日志范围内，需要进一步检查 prevLogTerm 是否匹配。
  // 情况3.1：prevLogIndex和prevLogTerm都匹配，还需要一个一个检查所有的当前新发送的日志匹配情况（有可能follower已经有这些新日志了）
  if (matchLog(args->prevlogindex(), args->prevlogterm())) {
    // 如果term也匹配。不能直接截断，必须一个一个检查，因为发送来的log可能是之前的，直接截断可能导致“取回”已经在follower日志中的条目
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

让我们简单分析下代码。



## 参考文献

[什么是分布式系统，这么讲不信你不会 - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/99932839)

[什么是分布式，分布式和集群的区别又是什么？这一篇让你彻底明白！_什么叫分布式-CSDN博客](https://blog.csdn.net/weixin_42046751/article/details/109510811)

[终于有人把分布式系统架构讲明白了-CSDN博客](https://blog.csdn.net/hzbooks/article/details/120916132)

[MIT6.824 Lab2 RAFT 介绍与实现 - pxlsdz - 博客园 (cnblogs.com)](https://www.cnblogs.com/pxlsdz/p/15557635.html)

[MIT6.5840(6.824) Lec06笔记: raft论文解读2: 恢复、持久化和快照 (gfx9.github.io)](https://gfx9.github.io/2024/01/12/MIT6.5840/Lec06笔记/)

[分布式一致性算法-Raft_分布式一致性算法raft-CSDN博客](https://blog.csdn.net/kiranet/article/details/121130250?spm=1001.2101.3001.6661.1&utm_medium=distribute.pc_relevant_t0.none-task-blog-2~default~BlogOpenSearchComplete~Rate-1-121130250-blog-140291635.235^v43^pc_blog_bottom_relevance_base5&depth_1-utm_source=distribute.pc_relevant_t0.none-task-blog-2~default~BlogOpenSearchComplete~Rate-1-121130250-blog-140291635.235^v43^pc_blog_bottom_relevance_base5&utm_relevant_index=1)