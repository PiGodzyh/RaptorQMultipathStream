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

## 视频传输功能

基于 H.264 NAL 直通 + RaptorQ FEC 的实时视频流传输。

### 特性

- **H.264 NAL 直通**: 不编解码，直接传输 MP4 文件中的 H.264 数据，带宽效率高 (~2-5MB/s vs ~93MB/s)
- **智能 FEC 策略**: I-帧 50% 冗余，P/B-帧 30% 冗余
- **保序播放**: 接收端按帧序号顺序写入，支持乱序缓存和丢帧处理
- **流ID分离**: 配置流(stream_id=0)与视频流(stream_id>=1)独立传输

### 快速开始

```bash
# 1. 准备测试视频（生成 5 秒 1280x720 H.264 测试视频）
ffmpeg -f lavfi -i testsrc=duration=5:size=1280x720:rate=30 -pix_fmt yuv420p input.mp4

# 2. 终端1 - 启动接收端
./build/video_streaming_demo receiver 9001 output.mp4

# 3. 终端2 - 启动发送端
./build/video_streaming_demo sender 127.0.0.1 9001 input.mp4

# 4. 按 Ctrl+C 结束（接收端会自动关闭视频文件）

# 5. 播放输出视频
ffplay output.mp4
```

### 使用说明

**接收端参数:**
```
./build/video_streaming_demo receiver <port> <output.mp4>
```
- `port`: 监听端口（默认 9001）
- `output.mp4`: 输出视频文件路径

**发送端参数:**
```
./build/video_streaming_demo sender <server_addr> <port> <input.mp4>
```
- `server_addr`: 接收端地址
- `port`: 接收端端口
- `input.mp4`: 输入视频文件（H.264 编码）

### 发送间隔参数

在 `video_transmit_params.h` 中调整：

```cpp
struct FrameTransmitParams {
    uint32_t send_interval_us = 10000;  // 10ms = 100fps 上限
    // ...
};
```

### 接收统计

接收端会实时输出帧接收统计：

```
[接收统计] [FrameStats] 总计:150 成功:150 丢弃(满):0 丢弃(旧):0 缓存:0 (成功率:100%)
```

| 指标 | 说明 |
|------|------|
| 总计 | 收到的帧总数 |
| 成功 | 成功写入视频的帧数 |
| 丢弃(满) | 缓存满丢弃的帧 |
| 丢弃(旧) | 过期的帧（已收到更新的帧） |
| 缓存 | 等待顺序到达的帧数 |

### 系统架构

```
发送端                                        接收端
┌─────────────────┐                          ┌─────────────────┐
│ VideoReader     │                          │ UDPServer       │
│ 读取 H.264 NAL  │                          │ 接收 UDP 包     │
│       ↓         │                          │       ↓         │
│ 区分 I/P/B 帧   │      UDP 包              │ Receiver        │
│       ↓         │  ═══════════════════►    │ RaptorQ 解码    │
│ RaptorQ 编码    │                          │       ↓         │
│ (I帧50%/PB30%)  │                          │ 按帧序排序      │
│       ↓         │                          │       ↓         │
│ UDPSender       │                          │ VideoWriter     │
│ 发送符号        │                          │ 写入 MP4        │
└─────────────────┘                          └─────────────────┘
```

### 关键技术点

**1. H.264 NAL 直通**
- 不解码视频，直接提取 MP4 中的 NAL 单元
- SPS/PPS 作为配置流先行发送（stream_id=0）
- 视频帧使用递增 stream_id（从1开始）

**2. 差异化 FEC**
- I-帧：符号大小 1024B，50% 冗余（容忍 33% 丢包）
- P/B-帧：符号大小 1024B，30% 冗余（容忍 23% 丢包）

**3. 保序交付**
- 接收端维护 `next_expected_frame_seq` 计数器
- 乱序帧缓存（最多 100 帧）
- 过期帧自动丢弃

## 多数据流传输（统一入口）

一个可执行文件管理四种数据类型的传输，端口分配如下：

| 数据类型 | 端口 | FEC策略 | 特性 |
|---------|------|---------|------|
| 飞控指令 | 9000 | 50%冗余，50ms超时 | 终端实时交互 |
| 视频流 | 9001 | I帧50%/P帧30%冗余 | H.264 NAL直通 |
| 点云 | 9002 | 10%冗余，8192B符号 | Livox CustomMsg格式 |
| 栅格地图 | 9003 | 20%冗余，4096B符号 | Eigen::Vector3d格式 |

### 使用方法

```bash
./build/multi_streaming_demo <receiver|sender> <type> [参数...]
```

**飞控指令（交互式）**
```bash
# 终端1 - 接收端
./build/multi_streaming_demo receiver fc 9000

# 终端2 - 发送端（实时输入）
./build/multi_streaming_demo sender fc 127.0.0.1 9000
# 输入指令如: TAKEOFF, LAND, MOVE 1.0 2.0 3.0
# 优先级: !high TAKEOFF, !normal HOVER, !low STATUS
```

**点云传输**
```bash
# 终端1 - 接收端
./build/multi_streaming_demo receiver pointcloud 9002

# 终端2 - 发送测试点云（1000点/帧，10帧）
./build/multi_streaming_demo sender pointcloud 127.0.0.1 9002 test

# 或发送PCD文件（从 data/pointcloud/ 读取）
./build/multi_streaming_demo sender pointcloud 127.0.0.1 9002 room_5k.pcd
```

**栅格地图传输**
```bash
# 终端1 - 接收端
./build/multi_streaming_demo receiver gridmap 9003

# 终端2 - 发送测试地图（100x100单元格）
./build/multi_streaming_demo sender gridmap 127.0.0.1 9003 test

# 或发送文件（从 data/gridmap/ 读取）
./build/multi_streaming_demo sender gridmap 127.0.0.1 9003 office_200x200.grid
```

**视频传输（使用统一入口）**
```bash
# 终端1 - 接收端
./build/multi_streaming_demo receiver video 9001 output.mp4

# 终端2 - 发送端（从 data/videos/ 读取）
./build/multi_streaming_demo sender video 127.0.0.1 9001 input.mp4
```

### 目录结构

```
data/                      # 输入文件目录
├── videos/
├── fc/
├── pointcloud/
└── gridmap/

output/                    # 输出文件目录
├── videos/
├── fc/                    # 飞控指令日志
├── pointcloud/            # 接收的点云PCD文件
└── gridmap/               # 接收的栅格地图文件
```

### 生成样例数据

```bash
# 生成点云和栅格地图样例数据
make gen-data
```

生成的样例数据：
- **点云**: `data/pointcloud/room_5k.pcd` (5000点，室内房间场景), `outdoor_10k.pcd` (10000点)
- **栅格地图**: `data/gridmap/office_200x200.grid` (10x10米办公室), `warehouse_300x300.grid` (15x15米仓库)
- **文本预览**: `data/gridmap/office_preview.txt` (ASCII艺术可视化)

### 故障排查

**视频无法播放**
```bash
# 检查输出文件是否完整
ffprobe output.mp4

# 强制重新封装修复
ffmpeg -i output.mp4 -c copy fixed.mp4
```

**接收端收不到帧**
- 确认端口未被占用：`lsof -i :9001`
- 检查输入视频格式：`ffprobe -v error -select_streams v:0 -show_entries stream=codec_name -of default=noprint_wrappers=1 input.mp4`
- 确保是 H.264 编码

**帧丢失过多**
- 增加冗余比例（修改 `video_common.h` 中的 `repair_ratio`）
- 降低发送速率（增大 `send_interval_us`）
- 检查网络带宽

## License

MIT

