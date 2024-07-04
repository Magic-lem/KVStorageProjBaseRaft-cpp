# Muduo初探

## Muduo网络库简介

Muduo是由【陈硕】大佬个人开发的TCP网络编程库，基于Reactor模式，提供了高效的事件驱动网络编程框架，有助于快速搭建高性能的网络服务端。

## 什么是Reactor模式？

### I/O多路复用

在网络I/O中，如果队每个连接都用一个独立的线程来处理，会导致大量的线程资源消耗。因此，出现了一种能够使用**一个线程来监听所有网络连接的I/O事件的方法 —— I/O多路复用**。

![在这里插入图片描述](https://img-blog.csdnimg.cn/21d3adc169964387ad0fd0c5ee9c0f5f.png#pic_center)

常见的I/O复用方法：select、poll、epoll。其中，epoll是一种`事件驱动`的I/O多路复用的方法。

![在这里插入图片描述](https://img-blog.csdnimg.cn/6c1e9ff9a53b46d4bbde92bfa7fe13b6.png#pic_center)

事件驱动的核心是，以**事件**为连接点，当有IO事件准备就绪时，**以事件的形式通知相关线程进行数据读写**，进而业务线程可以直接处理这些数据，这一过程的后续操作方，都是被动接收通知，看起来有点像回调操作。

这种模式下，I/O 读写线程、业务线程工作时，必有数据可操作执行，不会在 I/O 等待上浪费资源，这便是事件驱动的核心思想。

### Reactor模型

**Reactor 是事件驱动模型的一种实现**。

Reactor 模式由 `Reactor 线程`、`Handlers 处理器`两大角色组成，两大角色的职责分别如下：

- Reactor 线程的职责：主要负责连接建立、监听IO事件、IO事件读写以及将事件分发到Handlers 处理器。
- Handlers 处理器（业务处理）的职责：非阻塞的执行业务处理逻辑。

![在这里插入图片描述](https://img-blog.csdnimg.cn/20750ad35d1c4ae98513dbf57247d474.png?x-oss-process=image/watermark,type_d3F5LXplbmhlaQ,shadow_50,text_Q1NETiBA5p-P5rK5,size_20,color_FFFFFF,t_70,g_se,x_16#pic_center)

对于Reactor模型，将建立连接、IO等待/读写以及事件转发等操作**分阶段处理**，对于不同阶段采用响应的优化策略来提高性能，常见的有下列三种：

- 单线程模型
- 多线程模型（Worker线程池）
- 主从多线程模型

### 单线程Reactor模型

![在这里插入图片描述](https://img-blog.csdnimg.cn/530cea60c8d94c94ba625730128366c3.png?x-oss-process=image/watermark,type_d3F5LXplbmhlaQ,shadow_50,text_Q1NETiBA5p-P5rK5,size_20,color_FFFFFF,t_70,g_se,x_16#pic_center)

在单线程 Reactor 模式中，`Reactor` 和 `Handler` 都在同一条线程中执行。所有 I/O 操作（包括连接建立、数据读写、事件分发等）、业务处理，都是由一个线程完成的，逻辑非常简单，但也有十分明显的缺陷：

- 一个线程支持处理的连接数非常有限，CPU 很容易打满，**性能方面有明显瓶颈**

- 当多个事件被同时触发时，只要有一个事件没有处理完，其他后面的事件就无法执行，这就会造成**消息积压及请求超时**

- 线程在处理 I/O 事件时，Select **无法同时处理连接建立、事件分发等**操作

- 如果 I/O 线程一直处于满负荷状态，很可能造成**服务端节点不可用**

当其中某个 Handler 阻塞时，会导致其他所有的 Handler 都得不到执行。

在这种场景下，被阻塞的 Handler 不仅仅负责输入和输出处理的传输处理器，还**包括负责新连接监听的 Acceptor 处理器**，可能导致服务器无响应。这是一个非常严重的缺陷，导致单线程反应器模型在生产场景中使用得比较少。



### 多线程Reactor模型（Worker线程池）

![在这里插入图片描述](https://img-blog.csdnimg.cn/285e1938ff1f47579381a53ac8b17ead.png?x-oss-process=image/watermark,type_d3F5LXplbmhlaQ,shadow_50,text_Q1NETiBA5p-P5rK5,size_20,color_FFFFFF,t_70,g_se,x_16#pic_center)



Reactor 多线程模型**将<font color='red'>`业务逻辑`</font>交给多个线程进行处理**。除此之外，多线程模型其他的操作与单线程模型是类似的，比如**连接建立、IO事件读写以及事件分发**等都是由**一个线程**来完成。

当客户端有数据发送至服务端时，Select 会监听到可读事件，数据读取完毕后提交到业务线程池中并发处理。一般的请求中，耗时最长的一般是业务处理，所以用一个线程池（worker 线程池）来处理业务操作，在性能上的提升也是非常可观的。

当然，这种模型也有明显缺点，**连接建立、IO 事件读取以及事件分发完全有单线程处理**；比如当**<font color='cornflowerblue'>某个连接通过系统调用正在读取数据，此时相对于其他事件来说，完全是阻塞状态，新连接无法处理、其他连接的 IO、查询 IO 读写以及事件分发都无法完成</font>**。对于像 Nginx、Netty 这种对高性能、高并发要求极高的网络框架，这种模式便显得有些吃力了。因为，<font color='cornflowerblue'>**无法及时处理新连接、就绪的 IO 事件以及事件转发等。**</font>

### 主从多线程Reactor模型

![img](https://img2020.cnblogs.com/blog/1477786/202007/1477786-20200720093707733-1887930109.jpg)

主从 Reactor 模式中，分为了**`主 Reactor`** 和 **`从 Reactor`**，分别处理 **新建立的连接**、**IO读写事件/事件分发**。

1. 主 Reactor 可以解决同一时间大量新连接，将其注册到从 Reactor 上进行IO事件监听处理
2. IO事件监听相对新连接处理更加耗时，此处我们可以考虑**使用线程池来处理**。这样能充分利用多核 CPU 的特性，能使更多就绪的IO事件及时处理。

主从多线程模型由**多个 Reactor 线程**组成，每个 Reactor 线程都有独立的 Selector 对象。<font color='red'>**MainReactor 仅负责处理客户端连接的 Accept 事件**</font>，连接建立成功后将新创建的连接对象注册至 SubReactor。再由 <font color='red'>**SubReactor 分配线程池中的 I/O 线程与其连接绑定，它将负责连接生命周期内所有的 I/O 事件**</font>。

在海量客户端并发请求的场景下，主从多线程模式甚至可以**适当增加 SubReactor 线程的数量**，从而利用多核能力提升系统的吞吐量。****

### 总结

Reactor核心是围绕<font color='red'>`事件驱动`</font>模型

- 一方面监听并处理IO事件
- 另一方面将这些处理好的事件分发业务线程处理

多线程模式（多线程模式和主从多线程模式），其工作模式大致如下：

- 将负责数据传输处理的 IOHandler 处理器的执行放入独立的线程池中。这样，业务处理线程与负责新连接监听的反应器线程就能相互隔离，避免服务器的连接监听受到阻塞。
- 如果服务器为多核的 CPU，可以将反应器线程拆分为多个子反应器（SubReactor）线程；同时，引入多个选择器，并且为每一个SubReactor引入一个线程，一个线程负责一个选择器的事件轮询。这样充分释放了系统资源的能力，也大大提升了反应器管理大量连接或者监听大量传输通道的能力。

`Reactor（反应器）模式`是**高性能网络编程在设计和架构层面的基础模式**，算是基础的原理性知识。只有彻底了解反应器的原理，才能真正构建好高性能的网络应

### 版权声明

图片和部分内容摘自链接：[高性能网络编程之 Reactor 网络模型（彻底搞懂）_reactor网络模型-CSDN博客](https://blog.csdn.net/ldw201510803006/article/details/124365838)，[Reactor模型详解：单Reactor多线程与主从Reactor多线程 - -零 - 博客园 (cnblogs.com)](https://www.cnblogs.com/-wenli/p/13343397.html#:~:text=主 - 从Reactor多线程 主 - 从 reactor 模式,事件交给 sub-reactor 负责分发。 其中 sub-reactor 的数量，可以根据 CPU 的核数来灵活设置。)，如有侵权，请告知删除。



## Muduo基本架构

采用了**<font color='red'>主从多线程reactor模型 + 线程池</font>**的架构。`Main Reactor`只用于监听新的连接，在`accept`之后就会将这个连接分配到`Sub Reactor`上，由`子Reactor`负责连接的事件处理。线程池中维护了两个队列，一个**队伍队列**，一个**线程队列**，外部线程将任务添加到任务队列中，如果线程队列非空，则会唤醒其中一只线程进行任务的处理，相当于是**生产者和消费者模型**。

![在这里插入图片描述](https://img-blog.csdnimg.cn/1000cea689714585a6efa392958fc354.png?x-oss-process=image/watermark,type_d3F5LXplbmhlaQ,shadow_50,text_Q1NETiBA5p-P5rK5,size_20,color_FFFFFF,t_70,g_se,x_16#pic_center)





## Muduo基本代码结构

muduo库有**三个核心组件**支撑一个`Reactor`实现持续的监听一组`fd`，并根据每个`fd`上发生的事件调用相应的处理函数。这三个组件分别是`Channel类`、`Poller/EpollPoller类`以及`EventLoop类`。

`EventLoop`起到一个驱动循环的功能，`Poller`负责从事件监听器上获取监听结果。而`Channel类`则在其中起到了将`fd`及其相关属性封装的作用，将`fd`及其感兴趣事件和发生的事件以及不同事件对应的回调函数封装在一起，这样在各个模块中传递更加方便。接着EventLoop调用。

![image](https://img2022.cnblogs.com/blog/2937307/202209/2937307-20220920184001882-1703980542.png)

### EventLoop类

在Muduo网络库中，`EventLoop`类是核心组件之一。它的作用是**提供事件循环机制，**负责管理和调度各种事件（如I/O事件、定时器事件），确保这些事件能够及时被处理，从而实现高效的网络通信。

> `EventLoop`的工作流程大致如下：
>
> 1. 创建一个`EventLoop`对象。
> 2. 将需要监控的文件描述符和对应的事件类型注册到`EventLoop`中。
> 3. 启动事件循环，进入循环体。
> 4. 在循环体中，使用epoll等系统调用等待事件的发生。
> 5. 当有事件发生时，调用相应的回调函数来处理这些事件。
> 6. 重复步骤4和5，直到事件循环停止。

**主要属性**

```
looping_：标志事件循环是否正在运行。
quit_：标志是否需要退出事件循环。
poller_：指向Poller对象的指针，Poller封装了具体的I/O多路复用机制（如epoll）。
activeChannels_：存储当前活跃的Channel对象列表。
currentActiveChannel_：当前正在处理的Channel对象。
eventHandling_：标志是否正在处理事件。
callingPendingFunctors_：标志是否正在调用待处理的任务。
mutex_：用于保护任务队列的互斥锁。
pendingFunctors_：待处理任务的队列。
threadId_：运行该EventLoop的线程ID。
timerQueue_：定时器队列，用于管理定时器事件。
```

**主要方法**

```cpp
loop(): 启动事件循环。该方法会进入循环体，不断地等待并处理事件，直到调用quit()方法。
    
quit()：  退出事件循环。该方法会设置quit_标志，在安全的时机退出事件循环。
    
runInLoop(Functor cb)： 在当前事件循环中执行指定的任务。如果在其他线程中调用该方法，会将任务添加到pendingFunctors_队列中。
    
queueInLoop(Functor cb)：将任务添加到待处理任务队列中，在事件循环的下一次迭代时执行。
    
updateChannel(Channel channel)： 更新一个Channel，即在Poller中更新该Channel所关注的事件。
removeChannel(Channel channel)：移除一个Channel，即在Poller中取消对该Channel的关注。
hasChannel(Channel channel)：检查Poller中是否包含指定的Channel。

wakeup()：  唤醒事件循环，使其从阻塞状态中退出。通常用于在其他线程中添加任务后通知事件循环执行。

doPendingFunctors()：执行待处理任务队列中的所有任务。该方法在事件循环中调用，以确保所有任务都能在事件循环中安全地执行。

handleRead()：处理读事件，通常用于处理唤醒事件循环的事件。
handleError()：处理错误事件。

```

**事件循环工作流程**

> **启动事件循环**：
>
> - 调用`loop()`方法，进入事件循环。
>
> **等待事件**：
>
> - 调用`Poller`的`poll()`方法等待事件发生。
>
> **处理事件**：
>
> - 遍历活跃的`Channel`列表，调用每个`Channel`的事件处理回调函数。
>
> **处理定时器事件**：
>
> - 检查并处理到期的定时器事件。
>
> **执行待处理任务**：
>
> - 调用`doPendingFunctors()`方法，执行在其他线程中添加的任务。
>
> **重复循环**：
>
> - 重复上述步骤，直到调用`quit()`方法退出事件循环。

### Poller类

`Poller`类是`EventLoop`的一个重要组件，负责具体的I/O多路复用机制的封装。`Poller`类是一个**抽象基类**，它**提供了统一的接口，以便不同的具体实现（如使用`epoll`或`poll`）可以继承并实现这些接口（对应两个派生类：PollPoller和EpollPoller）**。具体使用中会根据环境变量的设置选择是使用epoll还是poll方法。`Poller`类的主要作用是**管理和分发I/O事件**。

**主要属性**

```
ownerLoop_：指向所属的EventLoop对象。确保Poller在正确的事件循环线程中被使用。
channels_：保存文件描述符到Channel对象的映射，便于快速查找。
```

**主要方法**

```
virtual Timestamp poll(int timeoutMs, ChannelList activeChannels) = 0*：   纯虚函数，等待事件发生并将活跃的Channel填充到activeChannels列
表中。timeoutMs指定等待事件的超时时间。

virtual void updateChannel(Channel channel) = 0*：纯虚函数，更新指定的Channel。通常涉及将该Channel的文件描述符和事件类型添加或修改到多路复用机制中。

virtual void removeChannel(Channel channel) = 0*：  纯虚函数，从多路复用机制中移除指定的Channel。

bool hasChannel(Channel channel) const*： 检查Poller中是否包含指定的Channel。通过查找channels_映射实现。
```

**主要工作流程**

> **创建并初始化`Poller`**：
>
> - 在创建`EventLoop`对象时，会根据系统和配置选择具体的`Poller`实现（如`EPollPoller`或`PollPoller`）。
>
> **添加、更新和移除`Channel`**：
>
> - 通过调用`updateChannel`和`removeChannel`方法，管理`Channel`对象及其关注的事件。
>
> **等待事件**：
>
> - 在事件循环中，调用<font color='red'>`poll`</font>方法等待事件发生，并获取活跃的`Channel`列表。
>
> **分发事件**：
>
> - 将活跃的事件分发给相应的`Channel`对象，`Channel`对象再调用预先设置的回调函数处理事件。

### Channel类

在 Muduo 网络库中，`Channel` 类是一个关键组件，用于**表示和管理文件描述符及其相关的 I/O 事件**。它负责**将底层的 I/O 事件和应用层的回调函数关联起来**，使得事件处理更加抽象和灵活。

**主要属性**

```cpp
loop_： 指向所属的 EventLoop 对象，确保 Channel 所属的事件循环。

fd_：文件描述符，表示这个 Channel 关注的具体文件描述符。

events_：表示当前 Channel 感兴趣的事件，由位掩码表示，如 EPOLLIN, EPOLLOUT 等。

revents_：表示实际发生的事件，由 EventLoop 设置。

index_：用于在 Poller 中记录 Channel 的状态（是否在 epoll 或 poll 的关注列表中）。

tied_：表示是否与某个对象（如 TcpConnection）绑定，防止对象在事件处理过程中被销毁。

eventHandling_：标志当前是否正在处理事件。

addedToLoop_：标志当前 Channel 是否已经添加到事件循环中。

readCallback_、writeCallback_、errorCallback_、closeCallback_：一系列的回调函数，用于处理不同类型的事件。
```

**主要方法**

1. **void handleEvent(Timestamp receiveTime);**

   ```
   用途：处理发生的事件，根据 revents_ 的值调用相应的回调函数。
   参数：receiveTime，表示事件发生的时间戳。
   实现逻辑：根据 revents_ 的值，依次调用错误、关闭、读、写事件的回调函数。
   ```

2. **setReadCallback**：设置读事件回调函数；**setWriteCallback**：设置写事件回调函数；**setErrorCallback**：设置错误事件回调函数；**setCloseCallback**：设置关闭事件回调函数。

   ```cpp
   void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }   // 设置读事件回调函数。
   void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }   // 设置写事件回调函数。
   void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }
   void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
   用途：将用户定义的回调函数绑定到相应的事件上。
   参数：回调函数对象，使用 std::function 封装。
   ```

3. **事件启用/禁用**

   ```cpp
   void enableReading() { events_ |= kReadEvent; update(); }
   void enableWriting() { events_ |= kWriteEvent; update(); }
   void disableReading() { events_ &= ~kReadEvent; update(); }
   void disableWriting() { events_ &= ~kWriteEvent; update(); }
   void disableAll() { events_ = kNoneEvent; update(); }
   用途：启用或禁用 Channel 对特定事件的监听。
   实现逻辑：修改 events_ 的值，并调用 update() 方法将修改通知给 Poller。
   ```

4. **update()**

   ```cpp
   用途：将 Channel 的状态更新到 Poller 中。
   实现逻辑：调用 EventLoop 的 updateChannel 方法，将当前 Channel 的状态同步到 Poller。
   void Channel::update() {
       addedToLoop_ = true;
       loop_->updateChannel(this);
   }
   ```

5. **remove()**

   ```cpp
   用途：从 Poller 中移除当前 Channel。
   实现逻辑：调用 EventLoop 的 removeChannel 方法，从 Poller 中删除当前 Channel。
   void Channel::remove() {
       addedToLoop_ = false;
       loop_->removeChannel(this);
   }
   ```

6. **tie()**

   ```cpp
   void tie(const std::shared_ptr<void>& obj);
   用途：将 Channel 绑定到一个对象（通常是 TcpConnection），防止在处理事件时该对象被销毁。
   参数：一个 std::shared_ptr<void> 对象。
   实现逻辑：存储 weak_ptr，在处理事件时检查对象是否有效。
   void Channel::tie(const std::shared_ptr<void>& obj) {
       tie_ = obj;
       tied_ = true;
   }
   ```

7. **其他辅助方法**

   ```cpp
   int fd() const { return fd_; }    // 返回文件描述符。
   int events() const { return events_; }   // 返回当前感兴趣的事件。
   void set_revents(int revt) { revents_ = revt; } // 设置实际发生的事件。
   bool isNoneEvent() const { return events_ == kNoneEvent; }  // 检查是否没有感兴趣的事件。
   ```

### TcpConnection类

在 Muduo 网络库中，`TcpConnection` 类是一个重要的组件，表示一个**已建立的TCP连接和控制该TCP连接的方法**（连接建立和关闭和销毁），以及这个TCP连接的服务端和客户端的**套接字地址信息**等。它封装了 **socket 文件描述符**，并提供了**处理连接的各种事件和操作的功能**。TcpConnection 类主要负责**管理连接的生命周期、数据的读写、回调函数的调用**等。

**主要属性**

```cpp
loop_：所属的 EventLoop 对象，表示这个连接归属于哪个事件循环。
name_：连接的名称，通常用来唯一标识一个连接。
state_：连接的状态，如连接建立、连接关闭等。
channel_：对应的 Channel 对象，用于管理 socket 的事件。
socket_：封装的 socket 文件描述符。
inputBuffer_：这是一个Buffer类，是该TCP连接对应的用户接收缓冲区。
outputBuffer_：也是一个Buffer类，不过是用于暂存那些暂时发送不出去的待发送数据. 因为Tcp发送缓冲区是有大小限制的，假如达到了高水位线，就没办法把发送的数据通过send()直接拷贝到Tcp发送缓冲区，而是暂存在这个outputBuffer_中，等TCP发送缓冲区有空间了，触发可写事件了，再把outputBuffer_中的数据拷贝到Tcp发送缓冲区中。
connetionCallback_、messageCallback_、writeCompleteCallback_、closeCallback_：用户会自定义 [连接建立/关闭后的处理函数] 、[收到消息后的处理函数]、[消息发送完后的处理函数]以及Muduo库中定义的[连接关闭后的处理函数]。这四个函数都会分别注册给这四个成员变量保存。
```

**主要方法**

1. **内部事件处理：handleRead()、handleWrite()、handleClose()、handleError()**

   私有成员方法，在一个已经建立好的TCP连接上主要会发生四类事件：可读事件、可写事件、连接关闭事件、错误事件。当事件监听器监**听到一个连接发生了以上的事件**，那么就会**在EventLoop中调用这些事件对应的处理函数**，同时accept返回已连接套接字所绑定的Channel中注册了这**四种回调函数**。

   ```cpp
   void handleRead(Timestamp receiveTime);
   void handleWrite();
   void handleClose();
   void handleError();
   // 处理读、写、关闭和错误事件，由 Channel 对象调用。
   ```

2. **连接状态的获取和设置**

   ```cpp
   const std::string& name() const { return name_; }
   const InetAddress& localAddress() const { return localAddr_; }
   const InetAddress& peerAddress() const { return peerAddr_; }
   bool connected() const { return state_ == kConnected; }
   // 获取连接的名称、本地地址、对端地址和连接状态。
   ```

3. **回调函数的设置**

   ```cpp
   void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
   void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
   void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }
   void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }
   // 设置连接、消息、写入完成和关闭的回调函数。
   ```

4. **发送数据**

   ```cpp
   void send(const std::string& message);
   void send(Buffer* buf);
   // 发送数据，将数据添加到输出缓冲区并调用 `handleWrite` 方法进行实际发送。
   ```

5. **关闭连接**

   ```cpp
   void shutdown();
   // 关闭写端，触发连接关闭过程。
   ```

6. **强制关闭**

   ```cpp
   void forceClose();
   // 强制关闭连接，不等待缓冲区的数据发送完毕。
   ```

7. **连接建立和销毁**

   ```cpp
   void connectEstablished();
   void connectDestroyed();
   // 连接建立时的初始化操作和连接销毁时的清理操作。
   ```

### Acceptor类

`Acceptor` 类在 Muduo 网络库中是一个关键组件，主要**负责接受新的客户端连接并将其传递给用户定义的回调函数**。`Acceptor` 封装了底层的监听 `socket` 和 `Channel`，并提供了对新连接的处理逻辑。

**主要属性**

```cpp
loop_：所属的 EventLoop 对象，表示这个 Acceptor 归属于哪个事件循环。
acceptSocket_：监听 socket，封装了底层的 socket 文件描述符。
acceptChannel_：用于监听事件的 Channel 对象。
listenning_：表示是否处于监听状态。
idleFd_：预留的文件描述符，用于处理文件描述符耗尽的情况。
newConnectionCallback_：新连接到达时的回调函数。
```

**主要方法**

1. **监听**

   ```cpp
   void listen();
   用途：启动监听 socket 开始接受新的连接。
   实现逻辑：调用 Socket 对象的 listen 方法，并将 acceptChannel 设置为可读状态以处理新的连接。
   ```

2. **设置回调函数**

   ```cpp
   void setNewConnectionCallback(const NewConnectionCallback& cb) { newConnectionCallback_ = cb; }
   用途：设置新连接到达时的回调函数。
   ```

3. **处理新连接**

   ```cpp
   void handleRead();
   用途：处理新的连接请求，接受连接并调用用户定义的回调函数。
   实现逻辑：调用 Socket 对象的 accept 方法获取新的连接文件描述符，并调用 newConnectionCallback_ 处理新连接。
   ```

### TcpServer类

`TcpServer` 类在 Muduo 网络库中是一个**高层次的网络服务器类**，负责**管理多个 TCP 连接**，并提供了一些便捷的方法来设置各种回调函数和启动服务器。它结合了 `Acceptor`、`TcpConnection` 等类来处理新的连接和数据传输。

**主要属性**

```cpp
loop_：主 EventLoop 对象，用于处理主线程中的事件。
ipPort_：服务器的 IP 和端口。
name_：服务器名称。
acceptor_：Acceptor 对象，负责接受新连接。
threadPool_：用于处理多个 EventLoop 线程的线程池。
connectionCallback_：连接建立或关闭时的回调函数。
messageCallback_：消息到达时的回调函数。
writeCompleteCallback_：数据写入完成时的回调函数。
connections_：管理所有活动连接的容器。
```

**主要方法**

```cpp
void start();  // 启动服务器

// 设置回调函数
void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }

void setThreadNum(int numThreads);  // 设置线程数量


void newConnection(int sockfd, const InetAddress& peerAddr); // 处理新连接

// 移除连接
void removeConnection(const TcpConnectionPtr& conn);
void removeConnectionInLoop(const TcpConnectionPtr& conn);
```



## Muduo简单使用

**使用muduo库编写一个简单的echo回显服务器**

```cpp
// 测试muduo库代码
// 使用muduo库实现一个简单的echo回显服务器
// ～ by magic_pri 2024.6.15

#include <muduo/net/TcpServer.h>
#include <muduo/base/Logging.h>
#include <boost/bind/bind.hpp>
#include <muduo/net/EventLoop.h>

// 使用muduo开发回显服务器
class EchoServer{
public:
    // 构造函数，输入参数
    // loop：EentLoop类指针，用于事件循环
    // listenAddr: InetAddress类对象，服务端的地址结构
    EchoServer(muduo::net::EventLoop* loop, const muduo::net::InetAddress& listenAddr);    
    
    void start();

private:

    // 连接建立或关闭时的回调函数
    void onConnection(const muduo::net::TcpConnectionPtr& conn);

    // 收到消息时的回调函数
    void onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, muduo::Timestamp time);

    muduo::net::TcpServer server_;  // 私有成员变量：TcpServer类对象，管理所有连接
};

// 构造函数实现：初始化TcpServer对象并设置回调函数
EchoServer::EchoServer(muduo::net::EventLoop* loop, 
                        const muduo::net::InetAddress& listenAddr)
                        : server_(loop, listenAddr, "EchoServer")   // 列表初始化成员变量
{   
    // 使用boost::bind绑定回调函数
    // [this]是捕获当前'EchoServer'对象的this指针，以便可以访问其成员函数
    // _1, _2, _3为占位符，表示回调函数应当传入的几个参数
    server_.setConnectionCallback(boost::bind(&EchoServer::onConnection, this, boost::placeholders::_1)); // 绑定建立或关闭连接时的回调函数
    server_.setMessageCallback(boost::bind(&EchoServer::onMessage, this, boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3));   // 绑定获取消息的回调函数
}

// start的实现：启动服务器，开始监听端口并接受新的连接
void EchoServer::start(){
    server_.start();
}

// 回调函数的实现
void EchoServer::onConnection(const muduo::net::TcpConnectionPtr& conn){
    // LOG_INFO，muduo库中的打印日志方法
    LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort() << " -> "
             << conn->localAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");
}

void EchoServer::onMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, 
                            muduo::Timestamp time){
    // 接收到所有消息，然后回显
    muduo::string msg(buf->retrieveAllAsString());  // 将缓冲区消息提取成muduo::string

    // 输出日志，记录接收到的消息
    LOG_INFO << conn->name() << " echo " << msg.size() << " bytes, " 
             << "data received at " << time.toString();

    // 回显，发送给对端（服务端发送给客户端，客户端发送给服务端）
    conn->send(msg);
}

int main(){                            
    // 打印日志，输出当前进程的PID
    LOG_INFO << "pid = " << getpid();

    // 创建循环事件对象
    muduo::net::EventLoop loop;

    // 创建InetAdress对象，表示服务器监听的地址和端口
    muduo::net::InetAddress listenAdrr(8888);

    // 创建EchoServer对象
    EchoServer server(&loop, listenAdrr);

    // 启动服务器，开始监听
    server.start();

    // 进入事件循环
    loop.loop();
}
```

**问题解答**

1. **代码如何运行？**

   ```bash
   # Step1：编译
   g++ main.cpp -lmuduo_net -lmuduo_base -lpthread -std=c++11 -o EchoServer
   
   # Step2：运行
   ./EchoServer
   # 运行成功后显示
   # 20240618 13::44:45.347367Z 34919 INFO  pid = 34919 - main.cpp:72
   
   # Step3：客户端访问
   # 新建一个终端窗口，访问本机器8888端口号
   echo "hello world" | ns localhost 8888
   ```

2. **`loop.loop()`事件循环的作用**

   如果不调用 `loop.loop()`，程序会在 `server.start()` 之后立即退出，服务器将无法接收和处理客户端连接。`loop.loop()` 是事件驱动服务器程序的核心，它使得程序进入事件循环，能够持续处理网络事件，保持服务器运行和响应客户端请求。没有这个调用，服务器将无法正常运行。

   Muduo 库中的 `EventLoop` 类封装了 I/O 多路复用和事件处理机制，提供了更高层次的接口。`loop.loop()` 实现了一个高效的事件循环，具备以下优势：

   - **高效的事件等待和处理**：使用 `epoll` 或其他高效的 I/O 多路复用机制来等待事件，并在事件发生时高效处理。
   - **自动管理回调函数**：`EventLoop` 自动管理回调函数的注册和调用，确保在正确的时间处理正确的事件。
   - **支持定时器和其他任务**：除了 I/O 事件，`EventLoop` 还支持定时器事件和其他需要在特定时间点处理的任务。
   - **更清晰的代码结构**：通过使用事件循环库，可以将事件等待和处理逻辑从业务逻辑中分离，使代码结构更清晰、维护更方便。

## 参考博客

本文部分内容/图片摘抄自以下博客，如有侵权，请告知删除：

[高性能网络编程之 Reactor 网络模型（彻底搞懂）_reactor网络模型-CSDN博客](https://blog.csdn.net/ldw201510803006/article/details/124365838)

[长文梳理muduo网络库核心代码、剖析优秀编程细节 - miseryjerry - 博客园 (cnblogs.com)](https://www.cnblogs.com/S1mpleBug/p/16712003.html#22-poller--epollpoller)

[muduo网络库学习总结：基本架构及流程分析_muduo主从reactor-CSDN博客](https://blog.csdn.net/moumde/article/details/114678084)

[长文梳理muduo网络库核心代码、剖析优秀编程细节 - miseryjerry - 博客园 (cnblogs.com)](https://www.cnblogs.com/S1mpleBug/p/16712003.html#前言)

[C++Muduo网络库：简介及使用-CSDN博客](https://blog.csdn.net/qq_42441693/article/details/128923253)

[C++ muduo网络库知识分享01 - Linux平台下muduo网络库源码编译安装-CSDN博客](https://blog.csdn.net/QIANGWEIYUAN/article/details/89023980)

