# Network 模块 - 基于 libevent 的 UDP 通信

这是一个基于 libevent 的 C++ UDP 网络通信模块，提供简洁易用的客户端和服务器接口。

## 功能特性

- ✅ 基于 libevent 事件驱动模型
- ✅ 异步非阻塞 I/O
- ✅ 简洁的 C++ 接口
- ✅ 支持回调机制
- ✅ 线程安全
- ✅ 易于集成

## 依赖

- **libevent** >= 2.0
  - macOS: `brew install libevent`
  - Ubuntu: `sudo apt-get install libevent-dev`
  - CentOS: `sudo yum install libevent-devel`

## 编译

```bash
# 编译所有目标
make

# 清理
make clean
```

**注意**: 所有编译生成的文件都在 `build/` 目录中。

## API 参考

### UDPServer - UDP 服务器

#### 构造函数

```cpp
UDPServer(uint16_t port, const std::string& bind_addr = "0.0.0.0")
```

#### 主要方法

- `void setReceiveCallback(ReceiveCallback callback)` - 设置接收回调
- `void setErrorCallback(ErrorCallback callback)` - 设置错误回调
- `bool start()` - 启动服务器（阻塞）
- `bool startAsync()` - 异步启动服务器
- `void stop()` - 停止服务器
- `ssize_t sendTo(data, addr, port)` - 发送数据到指定地址
- `ssize_t reply(packet)` - 回复数据包

#### 示例

```cpp
#include "network_server.h"
using namespace Network;

UDPServer server(8888);

// 设置接收回调
server.setReceiveCallback([&server](const Packet& packet) {
    std::cout << "收到数据: " << packet.data.size() << " 字节" << std::endl;
    std::cout << "来自: " << packet.remote_addr << ":" << packet.remote_port << std::endl;
    
    // 回复
    std::string reply = "收到";
    std::vector<uint8_t> reply_data(reply.begin(), reply.end());
    Packet reply_packet(reply_data, packet.remote_addr, packet.remote_port);
    server.reply(reply_packet);
});

// 启动服务器
server.start();
```

### UDPClient - UDP 客户端

#### 构造函数

```cpp
UDPClient(uint16_t bind_port = 0)  // 0 表示自动分配端口
```

#### 主要方法

- `ssize_t sendTo(data, addr, port)` - 发送数据到指定地址
- `void setDefaultTarget(addr, port)` - 设置默认目标
- `ssize_t send(data)` - 发送到默认目标
- `void setReceiveCallback(ReceiveCallback callback)` - 设置接收回调
- `bool startReceiving()` - 开始接收数据（可选）
- `void stopReceiving()` - 停止接收

#### 示例

```cpp
#include "network_client.h"
using namespace Network;

UDPClient client;

// 设置默认目标
client.setDefaultTarget("127.0.0.1", 8888);

// 发送数据
std::string message = "Hello, Server!";
std::vector<uint8_t> data(message.begin(), message.end());
client.send(data);

// 如果需要接收响应
client.setReceiveCallback([](const Packet& packet) {
    std::string text(packet.data.begin(), packet.data.end());
    std::cout << "收到响应: " << text << std::endl;
});
client.startReceiving();
```

### Packet - 数据包结构

```cpp
struct Packet {
    std::vector<uint8_t> data;      // 数据内容
    std::string remote_addr;        // 远程地址
    uint16_t remote_port;           // 远程端口
};
```

## 运行示例

### 启动服务器

```bash
# 在终端 1
build/server_example 8888
```

### 运行客户端

```bash
# 在终端 2
build/client_example 127.0.0.1 8888
```

或使用 make 命令：

```bash
# 终端 1
make run-server

# 终端 2
make run-client
```

## 使用场景

### 场景 1: 简单的请求-响应服务器

```cpp
UDPServer server(8888);

server.setReceiveCallback([&server](const Packet& packet) {
    // 处理请求
    std::vector<uint8_t> response = processRequest(packet.data);
    
    // 回复
    Packet reply(response, packet.remote_addr, packet.remote_port);
    server.reply(reply);
});

server.start();
```

### 场景 2: 定期发送数据的客户端

```cpp
UDPClient client;
client.setDefaultTarget("192.168.1.100", 9000);

while (running) {
    std::vector<uint8_t> data = collectData();
    client.send(data);
    std::this_thread::sleep_for(std::chrono::seconds(1));
}
```

### 场景 3: 双向通信

```cpp
// 服务器端
UDPServer server(8888);
server.setReceiveCallback([&server](const Packet& packet) {
    // 处理并回复
    server.reply(processAndReply(packet));
});
server.startAsync();

// 客户端
UDPClient client;
client.setDefaultTarget("server.example.com", 8888);

client.setReceiveCallback([](const Packet& packet) {
    std::cout << "收到服务器响应" << std::endl;
});
client.startReceiving();

// 发送请求
client.send(request_data);
```

## 集成到你的项目

### 方法 1: 链接静态库

```makefile
INCLUDES = -I/path/to/network
LDFLAGS = -L/path/to/network/build
LIBS = -lnetwork -levent -lpthread

your_program: your_code.o
	$(CXX) -o $@ $^ $(LDFLAGS) $(LIBS)
```

### 方法 2: 直接包含源文件

```makefile
SOURCES = your_code.cpp \
          network/network_server.cpp \
          network/network_client.cpp

your_program: $(SOURCES)
	$(CXX) -o $@ $(SOURCES) -levent -lpthread
```

## 性能提示

1. 对于高频率小包传输，考虑批量发送
2. 接收回调中避免长时间阻塞操作
3. 大数据传输建议分片发送
4. 考虑使用数据包序列号和确认机制

## 常见问题

### Q: 编译时找不到 libevent？

A: 确保安装了 libevent 开发库：
```bash
# macOS
brew install libevent

# Ubuntu/Debian
sudo apt-get install libevent-dev

# CentOS/RHEL
sudo yum install libevent-devel
```

### Q: 如何处理大于 64KB 的数据？

A: UDP 单个数据包大小受限，建议：
- 分片发送
- 添加序列号和重组逻辑
- 或考虑使用 TCP

### Q: 如何实现可靠传输？

A: UDP 本身不保证可靠性，需要在应用层实现：
- 确认机制（ACK）
- 超时重传
- 序列号
- 校验和

## 许可证

本模块可自由使用和修改。

## 更多信息

- libevent 文档: https://libevent.org/
- UDP 协议: RFC 768

