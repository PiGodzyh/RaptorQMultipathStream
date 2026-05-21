# RaptorQ 多流传输系统

基于 RaptorQ FEC 的多传感器数据实时传输系统，支持无人机/机器人遥操作场景下视频、语音、飞控指令、激光点云、栅格地图五种数据类型的并发可靠传输。

## 功能特性

- **RaptorQ FEC 前向纠错**：容忍丢包，支持差异化冗余策略（I帧 70%、视频 40%、语音 5%）
- **严格优先级调度**：按实时性要求排序出队——飞控指令 > 栅格地图 > 视频 > 语音 > 激光点云
- **应用层带宽预留**：Token Bucket bps 流量整形 + 接收端反馈闭环动态调整
- **五种数据类型并发**：独立 UDP 端口，独立编码线程，互不干扰
- **实时观看支持**：视频 HTTP MJPEG 流、点云 PCL Viewer 3D 显示

## 系统架构

```
发送端                                              接收端
┌─────────────────┐                                ┌─────────────────┐
│ Video/FC/Voice  │                                │ 5x Receiver     │
│ /PC/GridMap     │                                │ (端口9000-9004) │
│     ↓           │                                │     ↓           │
│ UnifiedSender   │      UDP (5个端口)             │ FeedbackSender  │
│ ├── BlockPartition                               │     ↓           │
│ ├── SendBuffer    │  ═══════════════════════►    │ RaptorQ 解码    │
│ │   (优先级队列    │                             │     ↓           │
│ │    + TokenBucket)│                            │ UnifiedReceiver │
│ ├── Scheduler     │                             │     ↓           │
│ ├── FeedbackController ◄═══════════════════════│ 回调专用接收器   │
│ └── 5x Sender     │    反馈包(500ms周期)         │ (Video/FC/...)  │
└─────────────────┘                                └─────────────────┘
```

## 数据类型与端口分配

| 数据类型 | 端口 | 实时性 | 可靠性 | 优先级 | 队列容量 | 带宽配额 | 动态调整 |
|---------|------|--------|--------|--------|---------|---------|---------|
| 飞控指令 | 9000 | 高 | 高 | 1st | 10 | 100kbps | ❌ 固定 |
| 栅格地图 | 9003 | 高 | 高 | 2nd | 300 | 500kbps | ✅ |
| 视频流 | 9001 | 中/高 | 中 | 3rd | 100 | 6000kbps | ✅ |
| 语音 | 9004 | 中 | 低 | 4th | 50 | 500kbps | ✅ |
| 激光点云 | 9002 | 低 | 中 | 5th | 500 | 1000kbps | ✅ |

## 编译

### 依赖

```bash
# Ubuntu/Debian
sudo apt-get install libevent-dev libavcodec-dev libavformat-dev libavutil-dev libpcl-dev

# 子模块编译
make -C libRaptorQ/build
make -C pack/build
make -C network/build
make -C event_base/build
```

### 编译主程序

```bash
# 完整编译（修改头文件后必须 clean）
make clean && make raptorq_demo

# 只编译（未修改头文件时）
make raptorq_demo
```

> ⚠️ **注意**：修改 `feedback.h`、`send_buffer.h` 等头文件后，`make` 不会自动重新编译所有依赖的 `.cpp`，**必须 `make clean`**。

## 运行方法

### 接收端

```bash
./build/raptorq_demo receiver
```

- 监听端口 9000-9004
- 自动创建输出目录 `output/`
- 按 `Ctrl+C` 停止

### 发送端

```bash
./build/raptorq_demo sender <目标IP> <数据类型> [选项]
```

**数据类型**（逗号分隔）：`fc`, `voice`, `video`, `pointcloud`, `gridmap`, `all`

**选项**：
- `--simulation`：使用模拟数据（默认使用真实文件）
- `--bypass-fec`：绕过 RaptorQ FEC，直接发送原始数据
- `--redundancy <ratio>`：设置 FEC 冗余度（0.0-1.0）
- `--drop-rate <rate>`：模拟网络丢包率（0.0-1.0）

**示例**：

```bash
# 发送全部 5 种真实数据
./build/raptorq_demo sender 127.0.0.1 all

# 只发送 FC + Video
./build/raptorq_demo sender 127.0.0.1 fc,video

# 发送视频（无 FEC）
./build/raptorq_demo sender 127.0.0.1 video --bypass-fec

# 50% 冗余度
./build/raptorq_demo sender 127.0.0.1 video --redundancy 0.5
```

**FC 交互式输入**：

发送端启动 `fc` 类型后，会进入交互模式：
```
> TAKEOFF
> MOVE 1.0 2.0 3.0
> LAND
> quit
```

优先级前缀：`!high <cmd>`, `!normal <cmd>`, `!low <cmd>`

---

### 视频实时观看（HTTP MJPEG 流）

一键启动接收端 + HTTP 服务器 + 发送端，浏览器直接观看实时视频：

```bash
./start_live_view.sh [发送端IP] [视频文件名]
```

**示例**：

```bash
# 本地测试（默认视频 data/videos/test_gop1s.mp4）
./start_live_view.sh

# 指定视频文件
./start_live_view.sh 127.0.0.1 test.mp4
```

然后用浏览器打开输出的地址：
```
http://<IP>:8080
```

**数据流**：
```
接收端(9001) → /tmp/video_live.h264 FIFO → ffmpeg 解码 MJPEG
                                                    ↓
浏览器 ← HTTP multipart/x-mixed-replace ← Python HTTP 服务器(8080)
```

> WSL2 环境下请使用脚本输出的 `http://<WSL-IP>:8080` 地址，Windows 浏览器通过 WSL 虚拟网卡访问。

**手动启动 HTTP 服务器（不通过脚本）**：

如果只想单独启动 HTTP 推流服务器，可使用标准库版本（无需额外依赖）：

```bash
python3 live_http_server.py
```

或 Flask 版本（需要 `.venv`）：

```bash
source .venv/bin/activate
python3 video_http_server.py
```

两者都监听 `0.0.0.0:8080`，功能相同。`live_http_server.py` 纯标准库实现，不依赖第三方包；`video_http_server.py` 基于 Flask，代码更简洁。

---

## 网络模拟（tc netem）

使用 `tc` 在 loopback 接口上模拟带宽限制和丢包：

```bash
# 设置 3mbit 带宽限制
sudo tc qdisc add dev lo root netem rate 3mbit

# 设置 3mbit + 20% 丢包
sudo tc qdisc add dev lo root netem rate 3mbit loss 20%

# 查看当前规则
sudo tc qdisc show dev lo

# 删除规则
sudo tc qdisc del dev lo root
```

---

## 测试脚本

### 1. 带宽阶梯测试（推荐）

自动跑 2mbit / 3mbit / 5mbit / 10mbit / clean 五组测试，输出 CSV 结果：

```bash
chmod +x run_bw_ladder_test.sh
./run_bw_ladder_test.sh
```

**输出**：`data/bw_ladder_result.csv`

| 列 | 含义 |
|---|---|
| bandwidth | tc 限速值 |
| fc_sent / fc_recv | FC 发送/接收数 |
| fc_arrival_pct | FC 到达率 |
| fc_delay_ms | FC 平均延迟 |
| video_sent / video_recv | Video 发送/接收帧数 |
| video_arrival_pct | Video 到达率 |
| video_jitter_ms | Video 帧间隔抖动（标准差）|
| video_pred_bw_kbps | Feedback 预测带宽 |

### 2. 单带宽测试（3mbit）

```bash
chmod +x test_bw_prediction.sh
./test_bw_prediction.sh
```

### 3. Clean 网络测试

```bash
chmod +x test_bw_prediction_clean.sh
./test_bw_prediction_clean.sh
```

---

## 日志文件

| 文件 | 内容 |
|------|------|
| `logs/fc_tx.log` | FC 发送：`timestamp_us,seq,priority,command` |
| `logs/video_tx.log` | Video 发送：`timestamp_us,frame_seq,frame_type,size` |
| `logs/video_rx.log` | Video 接收：`timestamp_us,frame_seq,frame_type,size` |
| `logs/sender.log` | Sender 完整日志（含 FeedbackController 调整记录） |
| `output/fc/received.log` | FC 接收：`timestamp_us,seq,priority,delay_ms,command` |

**分析命令**：

```bash
# FC 到达率
wc -l logs/fc_tx.log output/fc/received.log

# FC 平均延迟
awk -F',' '{sum+=$4} END {if(NR>0) printf "%.2fms\n", sum/NR}' output/fc/received.log

# Feedback 带宽调整记录
grep "FeedbackController.*Adjusted Video" logs/sender.log

# Video 帧间隔抖动
awk -F',' 'NR>1{d=$1-prev;sum+=d;sq+=d*d;n++} {prev=$1} END{m=sum/n;printf "%.2fms\n",sqrt(sq/n-m*m)/1000}' logs/video_rx.log
```

---

## 数据分析工具（Python）

`local/` 目录下提供了专用分析脚本，避免手写 `awk`：

### 1. FC 延迟与到达率统计

```bash
python3 local/calc_fc_delay.py
```

**输入**：`logs/fc_tx.log` + `output/fc/received.log`  
**输出**：逐行延迟、到达率、P50/P95/P99 分位数

```
统计结果:
  发送总数: 200
  成功接收: 199
  丢失:     1
  到达率:   99.5%

延迟统计:
  平均: 1.659 ms
  最小: 0.812 ms
  最大: 3.421 ms
  P50:  1.523 ms
  P95:  2.891 ms
  P99:  3.312 ms
```

### 2. Video 延迟与帧类型统计

```bash
python3 local/calc_video_delay.py
```

**输入**：`logs/video_tx.log` + `logs/video_rx.log`  
**输出**：按 I/P/B 帧分别统计延迟

```
视频统计:
  发送帧数: 150
  成功接收: 148
  丢失:     2
  到达率:   98.7%

延迟统计 (所有帧):
  平均: 2.134 ms
  P50:  1.987 ms

  I帧 (15帧): 平均 1.823 ms
  P帧 (133帧): 平均 2.201 ms
```

### 3. Feedback 自适应冗余度时间线

```bash
python3 local/analyze_feedback.py <receiver_log> <sender_log>
```

**示例**：
```bash
python3 local/analyze_feedback.py logs/receiver.log logs/sender.log
```

**输出**：
```
自适应冗余度变化时间线
  [2026-04-24 14:30:15]: 冗余度 = 40.0%, 速率 = 6000 kbps
  [2026-04-24 14:30:20]: 冗余度 = 40.0%, 速率 = 5700 kbps

分阶段统计
  发送帧数: 150
  接收帧数: 148
  整体成功率: 98.7%
```

---

## 目录结构

```
├── build/                    # 编译输出
│   └── raptorq_demo          # 主程序（统一入口）
├── data/                     # 输入数据
│   ├── videos/
│   ├── voice/
│   ├── pointcloud/
│   ├── gridmap/
│   └── fc/
├── output/                   # 接收端输出
│   ├── videos/
│   ├── voice/
│   ├── pointcloud/
│   ├── gridmap/
│   └── fc/
├── logs/                     # 运行日志
├── local/                    # 论文相关文档
│   ├── thesis_content_guide.md
│   ├── prompt_for_ai_writer.md
│   └── 设计期望.png
├── send_buffer.h/cpp         # 发送缓冲与 TokenBucket
├── scheduler.h/cpp           # 调度器
├── feedback.h/cpp            # 反馈闭环
├── unified_sender.h/cpp      # 统一发送器
├── unified_receiver.h/cpp    # 统一接收器
├── block_partition.h/cpp     # 数据分块
├── video_transmitter.cpp     # 视频发送
├── video_receiver.cpp        # 视频接收
├── fc_control.cpp            # 飞控指令
├── point_cloud.cpp           # 点云
├── grid_map.cpp              # 栅格地图
├── voice_transmitter.cpp     # 语音发送
├── voice_receiver.cpp        # 语音接收
├── network/                  # UDP 网络模块
├── event_base/               # 事件循环模块
├── pack/                     # RaptorQ 封装
├── libRaptorQ/               # RaptorQ 库
└── VideoCodec/               # H.264 读写
    └── VoiceCodec/           # PCM 编解码
```

---

## 系统配置参数

| 参数 | 数值 | 说明 |
|-----|------|------|
| Feedback 周期 | 500 ms | 接收端发送反馈包间隔 |
| EWMA 平滑系数 α | 0.3 | 新测量值权重 30% |
| 保守系数 | 0.95 | 建议速率打 95 折 |
| 下限保护 | 50% | 不低于初始配额的 50% |
| TokenBucket 突发因子 | 0.05 | 50ms 数据量 |
| Scheduler 调度周期 | 100 μs | 每次调度间隔 |

---

## 故障排查

### 编译错误：头文件修改后程序崩溃

```bash
# 必须 clean 后重新编译
make clean && make raptorq_demo
```

### 接收端 Segmentation Fault

检查 `receiver.o` 是否因头文件修改未重新编译：
```bash
ls -la build/receiver.o build/feedback.o
# 若 receiver.o 时间早于 feedback.o，说明未重新编译
make clean && make raptorq_demo
```

### 发送端报 "发送失败"

- 检查接收端是否已启动
- 检查 `tc` 规则是否过于严格：`sudo tc qdisc show dev lo`
- 清理 `tc` 规则测试：`sudo tc qdisc del dev lo root`

### 视频无法播放

```bash
# 检查输出文件
ffprobe output/videos/received.mp4

# 强制修复
ffmpeg -i output/videos/received.mp4 -c copy fixed.mp4
```

### 端口被占用

```bash
lsof -i :9000
kill -9 <PID>
```

---

## License

MIT
