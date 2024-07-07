# C++下Protobuf简单学习

Protobuf（[Protocol](https://so.csdn.net/so/search?q=Protocol&spm=1001.2101.3001.7020) Buffers）协议是一种由 Google 开发的`高效的`、`跨语言的`、`平台无关`的**数据序列化协议**，提供二进制序列化格式和相关的技术，它用于**高效地序列化和反序列化结构化数据**，通常用于网络通信、数据存储等场景。

Protobuf 在许多领域都得到了广泛应用，特别是在**分布式系统、RPC（Remote Procedure Call）框架和数据存储**中，它提供了一种高效、简洁和可扩展的方式来序列化和交换数据，Protobuf 的主要优点包括：

- **高效性**：Protobuf 序列化后的二进制数据通常比其他序列化格式（比如超级常用的JSON）更小，并且序列化和反序列化的速度更快，这对于性能敏感的应用非常有益。
- **简洁性**：Protobuf 使用一种定义消息格式的语法，它允许定义字段类型、顺序和规则（消息结构更加清晰和简洁）
- **版本兼容性**：Protobuf 支持向前和向后兼容的版本控制，使得在消息格式发生变化时可以更容易地处理不同版本的通信。
- **语言无关性**：Protobuf 定义的消息格式可以在多种编程语言中使用，这有助于跨语言的通信和数据交换（截至本文发布目前官方支持的有C++/C#/Dart/Go/Java/Kotlin/python）
- **自动生成代码**：Protobuf 通常与相应的工具一起使用，可以自动生成代码，包括序列化/反序列化代码和相关的类（减少了手动编写代码的工作量，提高效率）

## 消息定义

Protocol Buffer 消息`message`和服务`service`由程序员编写的 `.proto` 文件描述。下面显示了一个示例 `消息`：

```protobuf
syntax = "proto2";		// 指定正在使用proto2语法

message Person {
  optional string name = 1;
  optional int32 id = 2;
  optional string email = 3;
}
```

然后执行下列命令进行编译：

```sh
protoc --cpp_out=. person.proto
```

protoc 编译器对 `.proto` 文件进行处理，会生成两个文件：`person.pb.h` 和 `person.pb.cc`，以操作相应的 protocol buffer。其中`.proto`文件中的**每一个消息有一个对应的类**。

`.proto`文件中的类型和各个语言中的类型匹配：

![proto](G:\code\study\KVStorage\KVStorageProjBaseRaft-cpp\doc\figures\.proto.png)

## 服务定义

如果想要**将消息类型用在RPC(远程方法调用)系统**中，可以在`.proto`文件中定义一个RPC服务接口，protocol buffer 编译器将会根据所选择的不同语言生成服务接口代码及存根。

例如，想要定义**一个RPC服务并具有一个`Search`方法**，该方法能够接收 `SearchRequest`并返回一个`SearchResponse`，此时可以在`.proto`文件中进行如下定义：

```protobuf
syntax = "proto3";

message SearchRequest {
  string query = 1;
}

message SearchResponse {
  string result = 1;
}

service SearchService {
    //rpc 服务的函数名 （传入参数）返回（返回参数）
    rpc Search (SearchRequest) returns (SearchResponse);
}
```

对该`.proto`文件编译，会得到一个`.pb.h`和一个`.pb.cc`文件，包含<font color='red'>`SearchService`类</font>和<font color='red'>`SearchService_stub`类</font>。

> **`SearchService`类 —— 所定义的一个服务类**

`SearchService`类继承于`google::protobuf::Service`类，是一个**服务器端实现RPC服务的类**==（因此这个过程就是程序员在.proto中定义了服务、服务方法以及服务方法需要传入的参数、返回的参数，然后通过解析将其转换成具体的语言代码形成一个服务类，类中定义了方法和调用这个方法的接口）==，源码大致如下：

```cpp
class SearchSerive_Stub;

class SearchService : public ::google::protobuf::Service {
 public:
  SearchService();
  virtual ~SearchService();

  SearchService(const SearchService&) = delete;
  SearchService& operator=(const SearchService&) = delete;

  typedef SearchService_Stub Stub;

  static const ::google::protobuf::ServiceDescriptor* descriptor();
  virtual const ::google::protobuf::ServiceDescriptor* GetDescriptor() const;

  virtual void Search(::google::protobuf::RpcController* controller,
                      const ::search::SearchRequest* request,
                      ::search::SearchResponse* response,
                      ::google::protobuf::Closure* done);

  // implements Service ----------------------------------------------

  const ::google::protobuf::Message& GetRequestPrototype(
      const ::google::protobuf::MethodDescriptor* method) const override;
  const ::google::protobuf::Message& GetResponsePrototype(
      const ::google::protobuf::MethodDescriptor* method) const override;

  void CallMethod(const ::google::protobuf::MethodDescriptor* method,
                  ::google::protobuf::RpcController* controller,
                  const ::google::protobuf::Message* request,
                  ::google::protobuf::Message* response,
                  ::google::protobuf::Closure* done) override;

  const ::google::protobuf::ServiceDescriptor* service_descriptor() const override;
  void Shutdown() override;

 private:
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(SearchService);
};
```

下面是对这个类的详细说明：

- **内部类别名**`Stub`：利用`typedef`定义了一个`SearchService_Stub`类的别名`stub`，用于指向服务的客户端存根（Stub）。
- **静态方法**：
  - `descriptor()`：返回当前服务的描述符。
  - `GetDescriptor()`：返回当前服务的描述符。
- **虚方法声明**：
  - `Search()`虚函数：声明该方法接受一个 `SearchRequest` 请求，处理后将结果存入 `SearchResponse` 中，并在完成时调用 `done` 闭包。具体实现由派生类定义，此时生成的这个类主要用来作为基类。
- **Service 接口实现**：
  - `GetRequestPrototype()` 和 `GetResponsePrototype()`：根据给定的方法描述符，返回请求和响应消息的原型。
  - `CallMethod()`：根据参数输入中的方法描述符调用本服务中相应的 RPC 方法。
  - `service_descriptor()`：返回服务的描述符。
  - `Shutdown()`：实现服务关闭时的逻辑。

**`SearchService_stub`类 —— 客户端存根**

`SearchService_stub` 类继承自`SearchService`类，客户端使用的存根（Stub），用于通过网络调用远程服务器上定义的 RPC 方法。

```cpp
class SearchService_stub : public SearchService {
 public:
  SearchService_stub(::google::protobuf::RpcChannel* channel);
  SearchService_stub(::google::protobuf::RpcChannel* channel,
                   ::google::protobuf::Service::ChannelOwnership ownership);
  ~SearchService_stub();

  inline ::google::protobuf::RpcChannel* channel() { return channel_; }

  // implements kvServerRpc ------------------------------------------

  void Search(::google::protobuf::RpcController* controller,
                      const ::search::SearchRequest* request,
                      ::search::SearchResponse* response,
                      ::google::protobuf::Closure* done);
 private:
  ::google::protobuf::RpcChannel* channel_;
  bool owns_channel_;
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(kvServerRpc_Stub);
};

```

显然可以看出，`SearchService_stub` 在构造的时候主要需要传入一个`google::protobuf::RpcChannel`类型的指针。`RpcChannel`类负责**管理客户端与服务器之间的网络通信**，抽象了底层的网络细节，包括连接建立、数据传输、错误处理等。通过 `RpcChannel`，客户端可以与服务器建立连接并保持通信状态。

于是，客户端可以实例化 `SearchService_stub` 类，是存根可以通过传入的 `RpcChannel`对象与所连接的服务器进行通信。然后客户使用存根对象调用服务器上定义的 RPC 方法，例如 `Search` 方法。

```cpp
// 示例：准备请求和响应对象
search::SearchRequest request;
search::SearchResponse response;

// 设置请求内容，例如填充 request 对象
...

// 发起远程调用
stub.Search(nullptr, &request, &response, nullptr);
```



<font color='red'>**CallerMethod在其中发挥的作用？**</font>

这里涉及到了两个类型的`CallerMethod`方法：**`RpcChannel::CallerMethod`和`Service::CallerMethod`**.

在实际使用中，当客户端通过存根对象调用远程服务器上的 RPC 方法时，最终会执行到 `CallMethod` 方法。继续以上面的例子，方法内部**，它会通过 **<font color='cornflowerblue'>`RpcChannel` 对象 `CallMethod` 方法</font>**来执行实际的 RPC 调用过程。

```cpp
void SearchService_stub::Search(::google::protobuf::RpcController* controller,
                              const ::FooRequest* request,
                              ::FooResponse* response,
                              ::google::protobuf::Closure* done) {
  channel_->CallMethod(descriptor()->method(0),
                       controller, request, response, done);
}
```

具体来说，这个方法调用之后会经过以下几件事情：

1. **创建 `RpcController` 对象**：如果传入的 `controller` 参数为 `nullptr`，则会创建一个默认的 `RpcController` 对象。
2. **调用 `CallMethod` 方法**：`SearchService::Stub` 类内部的 `Search` 方法会调用 **`RpcChannel` 对象的 `CallMethod` 方法**，同时传递以下参数：
   - `method` 参数：指定要调用的 RPC 方法的描述符。
   - `controller` 参数：负责控制 RPC 调用过程，例如处理超时、错误处理等。
   - `request` 参数：包含了客户端发送的请求消息。
   - `response` 参数：用于存储服务器端处理后的响应消息。
   - `done` 参数：在 RPC 调用完成时调用的回调函数对象。
3. **执行 RPC 调用（在`RpcChannel::CallerMethod`中定义）**：在  `RpcChannel` 对象的 `CallMethod`方法内部，根据 `method` 参数确定要调用的具体 RPC 服务名称和方法名称（例如 `Search`），然后构建请求头，和对应的请求消息一起发送给服务器。
4. **处理响应（在服务端中定义）**：服务器端接收到消息后，根据请求头中的服务名称和方法名称，<font color='cornflowerblue'>**调用服务对象的`CallMethod`方法来处理请求**</font>，并且将生成的响应消息存储在响应消息`response` 参数中，并在完成时调用 `done` 对象通知调用方。

## 参考文献

上述内容多摘抄自以下博客，如有侵权，请联系删除：

[【保姆级】Protobuf详解及入门指南-CSDN博客](https://blog.csdn.net/aqin1012/article/details/136628117)

[概览 | 协议缓冲区文档 - ProtoBuf 中文](https://protobuf.com.cn/overview/)

[Protobuf 完整解析 - 公司最常用的数据交互协议 - hongxinerke - 博客园 (cnblogs.com)](https://www.cnblogs.com/zhenghongxin/p/10891426.html)

