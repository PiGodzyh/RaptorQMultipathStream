# Sender & Receiver Demo

基于 RaptorQ FEC 的可靠 UDP 数据传输系统。

## 功能特性

- ✅ **RaptorQ FEC 编码**: 前向纠错，容忍丢包
- ✅ **多线程架构**: 基于 libevent 的高性能事件循环
- ✅ **异步处理**: 非阻塞发送和接收
- ✅ **流管理**: 支持多数据流并发传输
- ✅ **自动解码**: 收到足够符号后自动恢复数据

## 系统架构

```
Sender (发送端)                     Receiver (接收端)
┌─────────────────────┐            ┌─────────────────────┐
│ 原始数据            │            │ UDP 接收            │
│       ↓             │            │       ↓             │
│ RaptorQ 编码        │            │ 符号提取            │
│       ↓             │            │       ↓             │
│ 生成符号            │   UDP      │ EventQueue         │
│ (源 + 修复)         │ ────────→  │       ↓             │
│       ↓             │            │ RaptorQ 解码        │
│ UDP 发送            │            │       ↓             │
│ (EventLoop)         │            │ 恢复数据            │
└─────────────────────┘            └─────────────────────┘
```

## 编译

### 依赖

```bash
# macOS
brew install libevent

# Ubuntu/Debian
sudo apt-get install libevent-dev

# CentOS/RHEL
sudo yum install libevent-devel
```

### 编译所有程序

```bash
make all
```

### 仅编译发送端或接收端

```bash
make sender      # 仅编译发送端
make receiver    # 仅编译接收端
```

## 使用方法

### 启动接收端

```bash
# 基本用法
./build/receiver_demo 9000

# 使用 8 个工作线程
./build/receiver_demo 9000 -t 8

# 指定输出目录
./build/receiver_demo 9000 -o ./received_data

# 详细模式
./build/receiver_demo 9000 -v

# 完整示例
./build/receiver_demo 9000 -t 8 -o ./output -v
```

**参数说明:**
- `port`: 监听端口（必需）
- `-t <count>`: 工作线程数（默认: 4）
- `-o <dir>`: 输出目录（默认: 当前目录）
- `-v`: 详细模式

### 启动发送端

```bash
# 基本用法
./build/sender_demo 127.0.0.1 9000

# 发送 20 个数据包，间隔 500ms
./build/sender_demo 127.0.0.1 9000 -n 20 -i 500

# 使用 512 字节符号，20% 冗余
./build/sender_demo 127.0.0.1 9000 -s 512 -r 0.2

# 完整示例
./build/sender_demo 127.0.0.1 9000 -n 50 -i 100 -s 256 -r 0.15
```

**参数说明:**
- `server_addr`: 目标地址（必需）
- `server_port`: 目标端口（必需）
- `-n <count>`: 发送数据包数量（默认: 10）
- `-i <ms>`: 发送间隔毫秒（默认: 100）
- `-s <size>`: 符号大小字节（默认: 256）
- `-r <ratio>`: 修复符号比例 0.0-1.0（默认: 0.1）
- `-t <threads>`: 编码线程数（默认: 4）

## 快速测试

### 方法1: 使用 make 命令

```bash
# 终端1: 启动接收端
make run-receiver

# 终端2: 启动发送端
make run-sender
```

### 方法2: 使用测试脚本

```bash
# 启动测试（会打开两个终端窗口）
./test.sh
```

### 方法3: 手动测试

```bash
# 终端1
./build/receiver_demo 9000 -t 4 -v

# 终端2
./build/sender_demo 127.0.0.1 9000 -n 10 -i 1000 -s 256 -r 0.1
```

## 测试场景

### 场景1: 基本功能测试

```bash
# 接收端
./build/receiver_demo 9000

# 发送端
./build/sender_demo 127.0.0.1 9000 -n 5 -i 2000
```

### 场景2: 高吞吐量测试

```bash
# 接收端（8个线程）
./build/receiver_demo 9000 -t 8

# 发送端（50个包，100ms间隔，512字节符号）
./build/sender_demo 127.0.0.1 9000 -n 50 -i 100 -s 512
```

### 场景3: 高冗余测试（模拟高丢包率）

```bash
# 接收端
./build/receiver_demo 9000 -v

# 发送端（30% 冗余）
./build/sender_demo 127.0.0.1 9000 -n 10 -r 0.3
```

## 输出示例

### 发送端输出

```
========================================
   Sender Demo (RaptorQ FEC)
========================================
目标地址: 127.0.0.1:9000
发送数量: 10
发送间隔: 1000 ms
符号大小: 256 字节
修复比例: 10%
========================================

开始发送数据...
✓ 已放入队列 #0 (大小: 1950 字节, 队列: 1)
Sender: 编码数据 1950 字节, 源符号: 8, 修复符号: 1
Sender: 流 0 发送完成, 成功: 9/9, 失败: 0
✓ 已放入队列 #1 (大小: 1950 字节, 队列: 0)
...
```

### 接收端输出

```
========================================
  Receiver Demo (RaptorQ FEC)
========================================
监听端口: 9000
工作线程: 4
========================================

[线程 0] 流 0 接收符号 #0 (1/9)
[线程 0] 流 0 接收符号 #1 (2/9)
...
[线程 0] 流 0 接收符号 #7 (8/9)
[线程 0] 流 0 可以解码，开始解码...
[线程 0] 流 0 解码成功！数据大小: 1950 字节

========================================
✓ 流 0 解码完成！
  数据大小: 1950 字节
  已保存到: ./received_stream_0000_msg_0.dat
========================================
```

## 清理

```bash
# 清理编译文件
make clean

# 深度清理（包括子模块）
make distclean
```

## 目录结构

```
src_new/
├── Makefile                 # 主 Makefile
├── README.md               # 本文件
├── test.sh                 # 测试脚本
├── common.h                # 公共头文件（PacketHeader）
├── sender.h/cpp            # 发送器实现
├── sender_demo.cpp         # 发送端 demo
├── receiver.h/cpp          # 接收器实现
├── receiver_demo.cpp       # 接收端 demo
├── event_base/            # 事件循环模块
│   ├── event_loop.h/cpp
│   ├── event_queue.h
│   └── Makefile
├── network/               # 网络模块
│   ├── network_server.h/cpp
│   ├── network_client.h/cpp
│   └── Makefile
├── pack/                  # RaptorQ 封装
│   ├── rq_pack.h/cpp
│   └── Makefile
└── build/                 # 编译输出
    ├── sender_demo        # 发送端可执行文件
    └── receiver_demo      # 接收端可执行文件
```

## 故障排查

### 问题1: 找不到 libevent

```bash
# 检查是否安装
brew list libevent  # macOS
dpkg -l | grep libevent  # Ubuntu

# 重新安装
brew install libevent  # macOS
```

### 问题2: 链接错误

```bash
# 清理重新编译
make distclean
make all
```

### 问题3: 接收端无法收到数据

- 检查防火墙设置
- 确认端口未被占用: `lsof -i :9000`
- 使用 `127.0.0.1` 进行本地测试

## 性能优化建议

1. **符号大小**: 较大的符号（512-1024字节）通常有更好的性能
2. **线程数**: 根据 CPU 核心数调整（通常为核心数的 1-2 倍）
3. **冗余比例**: 根据网络丢包率调整（10-30%）
4. **发送间隔**: 避免网络拥塞，根据带宽调整

## License

MIT

