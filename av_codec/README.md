# 音视频编解码模块 (av_codec)

本模块提供完整的音视频处理功能：
- 音视频编解码（使用 FFmpeg）
- 基础网络传输（使用 network 模块）
- 支持大文件分片传输
- 自动处理结束标记

## 模块组成

| 文件 | 说明 |
|------|------|
| `av_codec.h/cpp` | 核心编解码：MediaReader、MediaWriter、MediaPacket |
| `av_sender.h/cpp` | 音视频发送器（使用 network/UDPClient） |
| `av_receiver.h/cpp` | 音视频接收器（使用 network/UDPServer） |
| `av_sender_demo.cpp` | 发送端 demo（基础版本，无FEC） |
| `av_receiver_demo.cpp` | 接收端 demo（基础版本，无FEC） |

## 依赖

```bash
# macOS
brew install ffmpeg

# Ubuntu/Debian
sudo apt install libavcodec-dev libavformat-dev libswscale-dev libswresample-dev libavutil-dev
```

## 编译

```bash
cd av_codec

# 编译库
make all

# 编译 demo
make demo
```

## 特性

### 分片传输
- 大于 1400 字节的包自动分片发送
- 接收端自动重组分片
- 避免 UDP 包过大导致发送失败

### 缓冲模式
- 接收的包先缓存到内存
- 在 `finalize()` 时按原始顺序排序后写入
- 确保视频文件结构正确

### 结束标记
- 发送端发送完成后自动发送 `END_OF_STREAM` 标记
- 接收端收到标记后自动完成文件写入
- 无需手动按 Ctrl+C 停止

### 编码器标签保留
- 正确传递 `codec_tag`（如 hvc1, hev1）
- 确保 QuickTime Player 等播放器兼容

## 数据流

### 发送端
```
[媒体文件] -> MediaReader -> MediaPacket -> AVSender -> [分片] -> UDP
```

### 接收端
```
UDP -> [重组] -> AVReceiver -> MediaPacket -> [缓冲] -> MediaWriter -> [排序写入] -> [输出文件]
```

## API 使用

### MediaReader - 读取媒体文件

```cpp
#include "av_codec.h"
using namespace AVCodecModule;

MediaReader reader;
reader.open("input.mp4");

// 获取媒体信息
const MediaInfo& info = reader.getMediaInfo();
std::cout << "视频: " << info.video_width << "x" << info.video_height << std::endl;

// 获取媒体头（用于传输）
MediaHeader header = reader.getMediaHeader();

// 读取数据包
MediaPacket packet;
while (reader.readPacket(packet)) {
    // 处理 packet...
}

reader.close();
```

### MediaWriter - 写入媒体文件

```cpp
MediaWriter writer;

// 使用媒体头初始化（包含编码器参数）
writer.initialize(header);
writer.open("output.mp4");

// 写入数据包（缓冲模式：先缓存，finalize 时排序写入）
writer.writePacket(packet);

// 完成写入（排序并写入所有缓冲的包）
writer.finalize();
writer.close();

// 查看统计
std::cout << "视频包: " << writer.getVideoPacketCount() << std::endl;
std::cout << "关键帧: " << writer.getKeyFrameCount() << std::endl;
```

### AVSender - 发送音视频

```cpp
#include "av_sender.h"

AVSender sender("127.0.0.1", 9000);
sender.open("input.mp4");

// 阻塞发送所有数据（包括结束标记）
sender.sendAll();

// 统计
std::cout << "发送包数: " << sender.getSentPackets() << std::endl;
std::cout << "发送字节: " << sender.getSentBytes() << std::endl;

sender.close();
```

### AVReceiver - 接收音视频

```cpp
#include "av_receiver.h"

AVReceiver receiver(9000);
receiver.setOutputFile("output.mp4");
receiver.setMaxBufferSize(100);  // 可选：设置缓冲大小

// 阻塞接收（收到结束标记后自动完成）
receiver.start();

// 如果手动停止，需要调用 finalize
receiver.finalize();

// 统计
std::cout << "写入包数: " << receiver.getWrittenPackets() << std::endl;
```

## Demo 使用

### 编译

```bash
cd av_codec
make demo
```

### 运行（基础版本，无FEC）

```bash
# 终端1 - 先启动接收端
./build/av_receiver_demo 9000 output.mp4

# 终端2 - 再启动发送端
./build/av_sender_demo input.mp4 127.0.0.1 9000
```

### 参数说明

**发送端：**
```
./build/av_sender_demo <media_file> <server_addr> <server_port> [options]
  -i <ms>     发送间隔毫秒（默认: 0）
```

**接收端：**
```
./build/av_receiver_demo <port> <output_file> [options]
  -b <size>   最大缓冲包数（默认: 100）
```

## 与 RaptorQ FEC 结合

本模块可以与外层的 RaptorQ FEC 模块结合使用，提高传输可靠性：

```
带FEC版本（外层 demo）:
  [av_codec] -> [RaptorQ 编码] -> [network] -> [RaptorQ 解码] -> [av_codec]

基础版本（本模块 demo）:
  [av_codec] -> [network] -> [av_codec]
```

外层 demo 位于项目根目录：
- `build/av_sender_demo` - 带 FEC 的发送端
- `build/av_receiver_demo` - 带 FEC 的接收端

编译带 FEC 版本：
```bash
cd ..
make av
```

运行带 FEC 版本：
```bash
# 接收端
./build/av_receiver_demo 9000 output.mp4

# 发送端
./build/av_sender_demo input.mp4 127.0.0.1 9000 -r 0.2
```

## 支持的格式

- **输入**：MP4, MKV, AVI, MOV, MP3, WAV 等（任何 FFmpeg 支持的格式）
- **输出**：MP4, MKV, MOV 等（任何 FFmpeg 支持的容器格式）
- **编码**：H.264, H.265/HEVC, VP9, AAC, MP3 等

## 注意事项

1. **接收端先启动**：确保接收端在发送端之前启动
2. **端口一致**：发送端和接收端使用相同端口
3. **文件扩展名**：输出文件扩展名决定容器格式
4. **内存使用**：缓冲模式会使用约等于视频大小的内存
