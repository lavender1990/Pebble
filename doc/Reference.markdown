# Reference

----------
## 设计理念
Pebble立项的目的之一是对老系统、老框架的升级替换，改变之前的面向协议编程模型，推动面向服务的编程理念，同时设计上坚持机制和策略分离，减少框架本身的复杂度，实现高扩展和良好的兼容能力。

## 整体目标
以服务为核心打造高效的开发和运维体验，同时具备良好的兼容性和可扩展性，在软件生命周期做到承上启下。

## 分层介绍
### 整体结构
                   business
    ----------------------------------------  
          server      |     extension
    ----------------------------------------
         framework    |    
    ------------------|     thirdparty 
          common      |  

### 逻辑层次

		callback/services
	-------------------------
			processor
	-------------------------
			 message

## 重要接口介绍

- PebbleServer
> PebbleServer可以理解为服务端程序框架的参考实现，是pebble机制的一个汇总，基本上开发一个服务端程序需要的功能或能力都可以从PebbleServer的API获取，所以在开发前尽量浏览一遍PebbleServer的API，大概了解下它提供的能力。
 
- Message
> 通用的网络IO接口，屏蔽了底层通信，对上只暴露连接的handle，通过handle完成连接管理和消息收发。 

- MessageDriver
> 网络驱动接口，用来扩展传输协议，Message支持安装多个Driver，即支持多种传输协议，不同传输协议使用地址前缀区分。

- IProcessor
> 消息处理接口，主要提供消息分发能力。业务扩展的Processor接口可以实现IEventHandler接口，方便实现一些消息相关的通用处理（如统计等）。

- Naming
> 名字接口，定义了名字服务基本的能力，包括注册、注销、查询、监控，开源版本默认只带了基于zk的参考实现。

- Router
> 路由接口，定义了基本的路由策略，依赖Naming，一个router对象负责到一个名字所有实例的连接，并能感知实例状态变化。  
> 另外用户可新增自定义路由策略，实现IRoutePolicy接口即可。

- PebbleClient
> PebbleClient和PebbleServer都是pebble机制的一个汇总，不过PebbleClient没有主循环，比较适合当做SDK用，即在其他现有程序框架中使用pebble的部分能力。

- BroadcastMgr
> 广播管理器，主要职责是频道管理，创建或加入某个频道，实际是在这个频道名字下注册了自己的实例信息，广播消息本身是一个rpc调用，区别是一个rpc调用是点对点的，广播是一对多的，而且广播不需要回包。


## 使用介绍

### RPC
- 定义IDL文件
> 支持protobuf和thrift

- 生成代码
> PB定义的IDL，生成代码需要两个命令：  
> protoc --cpp_out=./ xxx.proto  
> protoc --plugin=protoc-gen-rpc=./protobuf_rpc --rpc_out=./ xxx.proto  
> 第一条命令生成基本的数据类型，第二条生成rpc接口和实现  
> 对pb版本没有要求，业务可自行选择pb版本

- 服务提供方实现服务接口，并添加到框架中
- 服务请求方创建桩对象
> 有几种方式创建桩对象  
> 1. 自行new  
> 2. PebbleServer::NewRpcClientByAddress  
> 3. PebbleServer::NewRpcClientByName  
> 4. PebbleServer::NewRpcClientByChannel  

- rpc调用方式
> 在协程中，使用同步接口  
> 不在协程中，使用异步接口

### 名字
一般情况业务不会直接用到名字，如果有使用场景，一般行为：
  
1. 通过PebbleServer获取名字实例  
2. 服务提供方注册名字  
3. 服务请求方根据名字查询地址列表，并监控名字  
4. 服务请求方根据获取的地址列表自行路由  

### 广播
广播实际是也是RPC调用，本质上RPC是点对点模型，广播是发布订阅模型，广播中一个重要概念就是频道，频道实际就是一个名字，用来组织订阅者集合，订阅者创建或加入频道后，实际是在这个名字下注册了一个实例节点，发布者像此频道广播消息，只需要拿到这个名字下所有实例地址即可，广播模块自己维护了实例变化及连接信息，发布者只需要一个rpc调用即可。  

另一个比较特殊是广播消息都是单向的，即不需要response的。

## 扩展

### 新增传输协议
在一些业务场景如对接第三方系统，无可避免需要兼容其他系统的传输协议，此时只需要实现MessageDriver接口，并注册到Message中即可使用。

不同传输协议通过地址前缀来区分（`pebble::MessageDriver::Prefix`），如`a_tcp://127.0.0.1:8000`和`b_tcp://127.0.0.1:9000`是两套协议。

### 新增应用协议

同上述场景，在对接第三方私有协议时，需要实现Processor来处理网络层丢上来的消息

如典型的msgid形式，processor收到消息解出msgid，然后根据msgid分发到业务回调（新实现的Processor可以提供注册函数来注册msgid的处理回调）



