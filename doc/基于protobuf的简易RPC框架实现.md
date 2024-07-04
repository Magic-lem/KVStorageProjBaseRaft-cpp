# 远程过程调用协议 RPC

## 什么是RPC？

RPC（Remote Procedure Call Protocol）远程过程调用协议。一个通俗的描述是：**客户端在不知道调用细节的情况下，调用存在于远程计算机上的某个对象，就像调用本地应用程序中的对象一样**。

正式描述：**一种通过网络从远程计算机程序上请求服务，而不需要了解底层网络技术的协议**。

几个要点：

- **RPC是协议：**既然是协议就只是一套规范，那么就需要有人遵循这套规范来进行实现。目前典型的RPC实现包括：Dubbo、Thrift、GRPC、Hetty等。
- **网络协议和网络IO模型对其透明**：既然RPC的客户端认为自己是在调用本地对象。那么传输层使用的是TCP/UDP还是HTTP协议，又或者是一些其他的网络协议它就不需要关心了。
- **信息格式对其透明**：我们知道在本地应用程序中，对于某个对象的调用需要传递一些参数，并且会返回一个调用结果。至于被调用的对象内部是如何使用这些参数，并计算出处理结果的，调用方是不需要关心的。那么对于远程调用来说，这些参数会以某种信息格式传递给网络上的另外一台计算机，这个信息格式是怎样构成的，调用方是不需要关心的。
- **应该有跨语言能力**：调用方实际上也不清楚远程服务器的应用程序是使用什么语言运行的。那么对于调用方来说，无论服务器方使用的是什么语言，本次调用都应该成功，并且返回值也应该按照调用方程序语言所能理解的形式进行描述。

![img](https://dj9c1bthhn.feishu.cn/space/api/box/stream/download/asynccode/?code=MTMwN2YxYWQ2Yjg5YjkwNGY4NzQwOTg5YzZiNWVhMGFfbzZaUmxCMEpQRHp4dzEzaGdVQ3hzNHZzRnNadFlaUERfVG9rZW46VlE1eWJmRlVub2wzbEx4ZThFWWNlanJGbjdjXzE3MTg4OTA1NDQ6MTcxODg5NDE0NF9WNA)

## 为什么要用RPC？

应用开发到一定的阶段的强烈需求驱动的。如果我们开发简单的单一应用，逻辑简单、用户不多、流量不大，那我们用不着。当我们的系统访问量增大、业务增多时，我们会发现一台单机运行此系统已经无法承受。此时，我们可以将业务拆分成几个互不关联的应用，分别部署在各自机器上，以划清逻辑并减小压力。此时，我们也可以不需要RPC，因为应用之间是互不关联的。

当我们的业务越来越多、应用也越来越多时，自然的，我们会发现有些功能已经不能简单划分开来或者划分不出来。此时，**可以将公共业务逻辑抽离出来，将之组成独立的服务Service应用 。而原有的、新增的应用都可以与那些独立的Service应用 交互，以此来完成完整的业务功能**。

所以此时，我们急需**一种高效的应用程序之间的通讯手段来**完成这种需求，所以你看，RPC大显身手的时候来了！

其实描述的场景也是**服务化 、微服务和分布式系统架构**的基础场景。即RPC框架就是实现以上结构的有力方式。

> **一些常用的RPC框架：**
>
> - **Thrift：**thrift是一个软件框架，用来进行可扩展且跨语言的服务的开发。它结合了功能强大的软件堆栈和代码生成引擎，以构建在 C++, Java, Python, PHP, Ruby, Erlang, Perl, Haskell, C#, Cocoa, JavaScript, Node.js, Smalltalk, and OCaml 这些编程语言间无缝结合的、高效的服务。
> - **gRPC：**一开始由 google 开发，是一款语言中立、平台中立、开源的远程过程调用(RPC)系统。
> - **Dubbo：**Dubbo是一个分布式服务框架，以及SOA治理方案。其功能主要包括：高性能NIO通讯及多协议集成，服务动态寻址与路由，软[负载均衡](https://cloud.tencent.com/product/clb?from_column=20065&from=20065)与容错，依赖分析与降级等。Dubbo是阿里巴巴内部的SOA服务化治理方案的核心框架，Dubbo自2011年开源后，已被许多非阿里系公司使用。
> - **Spring Cloud：**Spring Cloud由众多子项目组成，如Spring Cloud Config、Spring Cloud Netflix、Spring Cloud Consul 等，提供了搭建分布式系统及微服务常用的工具，如配置管理、服务发现、断路器、智能路由、微代理、控制总线、一次性token、全局锁、选主、分布式会话和集群状态等，满足了构建微服务所需的所有解决方案。Spring Cloud基于Spring Boot, 使得开发部署极其简单。

## RPC的原理

**一个RPC调用的流程涉及到的通信细节：**

1. 服务消费方（client）调用以本地调用方式调用服务；
2. client stub接收到调用后负责将方法、参数等组装成能够进行网络传输的消息体；
3. client stub找到服务地址，并将消息发送到服务端；
4. server stub收到消息后进行解码；
5. server stub根据解码结果调用本地的服务；
6. 本地服务执行并将结果返回给server stub；
7. server stub将返回结果打包成消息并发送至消费方；
8. client stub接收到消息，并进行解码；
9. 服务消费方得到最终结果。

![img](https://dj9c1bthhn.feishu.cn/space/api/box/stream/download/asynccode/?code=MjkyZTRjMThiZDVmMjg4MmQ5MTQ1Y2MyNzgyNjQ4MmRfc2VOZHBTUkc4TEp1RmxBcWgzMzZPY1RyVVNla2ZJYUxfVG9rZW46VmZRc2I2Vmx4b0N5azV4dGI2MmNmYkJjbmtlXzE3MTg4OTA1NDQ6MTcxODg5NDE0NF9WNA)

RPC的目标就是要**2~8这些步骤都封装起来，让用户对这些细节透明**。



# 基于Protobuf的简易RPC框架实现

## 什么是Protobuf？

Protobuf（[Protocol](https://so.csdn.net/so/search?q=Protocol&spm=1001.2101.3001.7020) Buffers）协议是一种由 Google 开发的`高效的`、`跨语言的`、`平台无关`的**数据序列化协议**，提供二进制序列化格式和相关的技术，它用于**高效地序列化和反序列化结构化数据**，通常用于网络通信、数据存储等场景。

Protobuf 在许多领域都得到了广泛应用，特别是在**分布式系统、RPC（Remote Procedure Call）框架和数据存储**中，它提供了一种高效、简洁和可扩展的方式来序列化和交换数据，Protobuf 的主要优点包括：

- **高效性**：Protobuf 序列化后的二进制数据通常比其他序列化格式（比如超级常用的JSON）更小，并且序列化和反序列化的速度更快，这对于性能敏感的应用非常有益。
- **简洁性**：Protobuf 使用一种定义消息格式的语法，它允许定义字段类型、顺序和规则（消息结构更加清晰和简洁）
- **版本兼容性**：Protobuf 支持向前和向后兼容的版本控制，使得在消息格式发生变化时可以更容易地处理不同版本的通信。
- **语言无关性**：Protobuf 定义的消息格式可以在多种编程语言中使用，这有助于跨语言的通信和数据交换（截至本文发布目前官方支持的有C++/C#/Dart/Go/Java/Kotlin/python）
- **自动生成代码**：Protobuf 通常与相应的工具一起使用，可以自动生成代码，包括序列化/反序列化代码和相关的类（减少了手动编写代码的工作量，提高效率）

> 参考博客：[【保姆级】Protobuf详解及入门指南-CSDN博客](https://blog.csdn.net/aqin1012/article/details/136628117)

## 简易RPC框架的实现

