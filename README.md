# RaptorQ FEC 数据传输系统

基于 RaptorQ FEC 的可靠 UDP 数据传输系统，支持音视频文件传输。

## 功能特性

- ✅ **RaptorQ FEC 编码**: 前向纠错，容忍丢包
- ✅ **多线程架构**: 基于 libevent 的高性能事件循环
- ✅ **异步处理**: 非阻塞发送和接收
- ✅ **流管理**: 支持多数据流并发传输
- ✅ **自动解码**: 收到足够符号后自动恢复数据
- ✅ **音视频传输**: 支持 MP4、MKV、MOV 等格式的实时传输

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

## 模块组成

```
RaptorQStream/
├── sender_demo.cpp         # 数据发送 demo
├── receiver_demo.cpp       # 数据接收 demo
├── av_sender_demo.cpp      # 音视频发送 demo（带FEC）
├── av_receiver_demo.cpp    # 音视频接收 demo（带FEC）
├── event_base/             # 事件循环模块
├── network/                # 网络模块（UDP）
├── pack/                   # RaptorQ FEC 封装
└── av_codec/               # 音视频编解码模块
    ├── av_sender_demo.cpp  # 音视频发送 demo（无FEC）
    └── av_receiver_demo.cpp # 音视频接收 demo（无FEC）
```

## 依赖

```bash
# macOS
brew install libevent ffmpeg

# Ubuntu/Debian
sudo apt-get install libevent-dev libavcodec-dev libavformat-dev libswscale-dev libswresample-dev libavutil-dev
```

## 编译

```bash
# 编译所有程序（包括音视频 demo）
make all

# 仅编译基础数据传输
make sender receiver

# 仅编译音视频传输 demo
make av

# 编译 av_codec 基础版本（无FEC）
make av-base
```

## 音视频传输使用

### 带 FEC 版本（推荐，适合不稳定网络）

```bash
# 终端1 - 先启动接收端
./build/av_receiver_demo 9000 output.mp4

# 终端2 - 再启动发送端
./build/av_sender_demo input.mp4 127.0.0.1 9000
```

**发送端参数：**
```
./build/av_sender_demo <media_file> <server_addr> <server_port> [options]
  -s <size>      RaptorQ符号大小字节（默认: 1024）
  -r <ratio>     FEC修复符号比例 0.0-1.0（默认: 0.2）
  -t <threads>   RaptorQ编码线程数（默认: 4）
```

**接收端参数：**
```
./build/av_receiver_demo <port> <output_file> [options]
  -t <count>     RaptorQ解码线程数量（默认: 4）
  -v             详细模式
```

### 基础版本（无FEC，适合稳定网络）

```bash
cd av_codec
make demo

# 终端1 - 接收端
./build/av_receiver_demo 9000 output.mp4

# 终端2 - 发送端
./build/av_sender_demo input.mp4 127.0.0.1 9000
```

### 音视频传输特性

- **分片传输**：大于 1400 字节的包自动分片，避免 UDP 发送失败
- **缓冲模式**：接收的包先缓存，完成后排序写入，确保文件结构正确
- **结束标记**：发送完成后自动发送结束标记，接收端自动完成文件
- **编码器兼容**：保留原始 codec_tag，确保 QuickTime 等播放器兼容

### 支持的格式

- **输入**：MP4, MKV, AVI, MOV, MP3, WAV 等
- **输出**：MP4, MKV, MOV 等
- **编码**：H.264, H.265/HEVC, VP9, AAC, MP3 等

## 基础数据传输使用

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
```

**参数说明:**
- `server_addr`: 目标地址（必需）
- `server_port`: 目标端口（必需）
- `-n <count>`: 发送数据包数量（默认: 10）
- `-i <ms>`: 发送间隔毫秒（默认: 1000）
- `-s <size>`: 符号大小字节（默认: 256）
- `-r <ratio>`: 修复符号比例 0.0-1.0（默认: 0.1）

## 快速测试

### 音视频传输测试

```bash
# 终端1 - 接收端
./build/av_receiver_demo 9000 output.mp4

# 终端2 - 发送端（20% FEC 冗余）
./build/av_sender_demo video.mp4 127.0.0.1 9000 -r 0.2
```

### 基础数据传输测试

```bash
# 终端1: 启动接收端
make run-receiver

# 终端2: 启动发送端
make run-sender
```

## 输出示例

### 音视频发送端输出

```
========================================
   AV Sender Demo (带 RaptorQ FEC)
   av_codec -> RaptorQ -> Network
========================================
媒体文件: video.mp4
目标地址: 127.0.0.1:9000
RaptorQ符号大小: 1024 字节
FEC修复比例: 20%
========================================

[1] 打开媒体文件...
媒体信息:
  视频: 1920x1080
  编码: hevc
  时长: 60.5 秒

[3] 发送媒体头...
✓ 媒体头已发送 (256 字节)

[4] 开始发送媒体数据 (RaptorQ 编码)...
进度: 视频=1800, 音频=0, 队列=5

[6] 发送结束标记...
已发送结束标记 (1/3)
已发送结束标记 (2/3)
已发送结束标记 (3/3)

========================================
发送完成统计:
  视频包数: 1800
  原始数据: 45.2 MB
  RaptorQ符号: 12500
  总耗时: 62.3 秒
========================================
```

### 音视频接收端输出

```
========================================
   AV Receiver Demo (带 RaptorQ FEC)
   Network -> RaptorQ -> av_codec
========================================
监听端口: 9000
输出文件: output.mp4
========================================

[1] 初始化网络接收器和 RaptorQ 解码器...

✓ 收到媒体头，av_codec 写入器已初始化
  视频: 1920x1080
  编码: hevc

进度: 视频=1800, 音频=0, 缓冲=1800

✓ 收到结束标记

[3] 完成输出...
正在排序并写入文件...
写入完成: 1800/1800 (失败: 0)
✓ 输出文件已保存: output.mp4

========================================
接收完成统计:
  视频包数: 1800
  关键帧数: 60
  数据总量: 45.2 MB
  输出文件: output.mp4
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
RaptorQStream/
├── Makefile                 # 主 Makefile
├── README.md                # 本文件
├── test.sh                  # 测试脚本
├── common.h                 # 公共头文件
├── sender.h/cpp             # RaptorQ 发送器
├── sender_demo.cpp          # 数据发送 demo
├── receiver.h/cpp           # RaptorQ 接收器
├── receiver_demo.cpp        # 数据接收 demo
├── av_sender_demo.cpp       # 音视频发送 demo（带FEC）
├── av_receiver_demo.cpp     # 音视频接收 demo（带FEC）
├── event_base/              # 事件循环模块
│   ├── event_loop.h/cpp
│   └── event_queue.h
├── network/                 # 网络模块
│   ├── network_server.h/cpp
│   └── network_client.h/cpp
├── pack/                    # RaptorQ FEC 封装
│   └── rq_pack.h/cpp
├── av_codec/                # 音视频编解码模块
│   ├── av_codec.h/cpp       # 核心编解码
│   ├── av_sender.h/cpp      # 音视频发送器
│   ├── av_receiver.h/cpp    # 音视频接收器
│   ├── av_sender_demo.cpp   # 基础版发送 demo
│   └── av_receiver_demo.cpp # 基础版接收 demo
└── build/                   # 编译输出
    ├── sender_demo          # 数据发送端
    ├── receiver_demo        # 数据接收端
    ├── av_sender_demo       # 音视频发送端（带FEC）
    └── av_receiver_demo     # 音视频接收端（带FEC）
```

## 故障排查

### 问题1: 音视频文件无法播放

- 确保使用带 FEC 版本进行传输
- 检查接收端是否收到结束标记并完成文件
- 使用 `ffprobe output.mp4` 检查文件完整性

### 问题2: 找不到 libevent 或 FFmpeg

```bash
# macOS
brew install libevent ffmpeg

# Ubuntu
sudo apt-get install libevent-dev libavcodec-dev libavformat-dev
```

### 问题3: 接收端无法收到数据

- 检查防火墙设置
- 确认端口未被占用: `lsof -i :9000`
- 使用 `127.0.0.1` 进行本地测试

### 问题4: 传输速度慢

- 减少 FEC 冗余比例：`-r 0.1`
- 增加符号大小：`-s 2048`
- 增加编码线程：`-t 8`

## 性能优化建议

1. **符号大小**: 较大的符号（1024-2048字节）通常有更好的性能
2. **线程数**: 根据 CPU 核心数调整（通常为核心数的 1-2 倍）
3. **冗余比例**: 根据网络丢包率调整
   - 稳定网络: 10%
   - 普通网络: 20%
   - 高丢包网络: 30%
4. **发送间隔**: 避免网络拥塞，根据带宽调整

## License

MIT
