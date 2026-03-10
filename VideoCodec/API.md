# VideoCodec API 说明 (H.264 NAL Passthrough)

## 概述

VideoCodec 模块提供基于 FFmpeg 的 H.264 视频数据读取/写入功能，**不解码为 YUV**，直接处理 H.264 NAL 单元。

**适用场景：**
- 视频网络传输（配合 RaptorQ FEC）
- 视频文件复制/转封装
- 需要直接处理 H.264 数据的场景

**优势：**
- 相比 YUV 透传，带宽节省约 20-50 倍
- 保留 I/P/B 帧信息，支持差异化传输
- 无需编解码，CPU 占用低

## 核心组件

| 组件 | 功能 |
|------|------|
| `VideoReader` | 从 MP4 读取 H.264 NAL 单元 |
| `VideoWriter` | 将 H.264 NAL 单元写入 MP4 |

## 数据结构

### EncodedFrame - 编码后的视频帧

```cpp
struct EncodedFrame {
    std::vector<uint8_t> data;      // H.264 NAL 单元数据
    int64_t pts;                    // 显示时间戳（毫秒）
    int64_t dts;                    // 解码时间戳（毫秒）
    bool is_key_frame;              // 是否关键帧
    FrameType type;                 // 帧类型 (I/P/B)
    uint32_t gop_id;                // GOP 编号
};
```

### FrameType - 帧类型

```cpp
enum class FrameType : uint8_t {
    UNKNOWN = 0,
    I_FRAME = 1,    // 关键帧（IDR）
    P_FRAME = 2,    // 前向预测帧
    B_FRAME = 3,    // 双向预测帧
};
```

### VideoInfo - 视频信息

```cpp
struct VideoInfo {
    std::string format_name;        // 格式名称
    std::string codec_name;         // 编码器名称
    int width, height;              // 分辨率
    int fps_num, fps_den;           // 帧率
    int64_t duration_ms;            // 时长
    int64_t bitrate;                // 比特率
    int gop_size;                   // GOP 大小
    std::vector<uint8_t> extradata; // SPS/PPS 数据
};
```

## VideoReader - 视频读取器

### 基本用法

```cpp
#include "video_reader.h"
using namespace VideoCodec;

VideoReader reader;
if (reader.Open("input.mp4")) {
    // 获取视频信息
    VideoInfo info = reader.GetVideoInfo();
    std::cout << "分辨率: " << info.width << "x" << info.height << std::endl;
    std::cout << "GOP大小: " << info.gop_size << std::endl;
    
    // 逐帧读取
    EncodedFrame frame;
    while (reader.ReadFrame(frame)) {
        // frame.data 包含 H.264 NAL 单元
        // frame.is_key_frame 标识是否为 I 帧
        // frame.type 标识为 I/P/B 帧
        
        if (frame.type == FrameType::I_FRAME) {
            // I 帧处理（高优先级）
        } else {
            // P/B 帧处理
        }
    }
    
    reader.Close();
}
```

### 主要接口

| 接口 | 说明 |
|------|------|
| `Open(filepath)` | 打开视频文件 |
| `Close()` | 关闭文件 |
| `IsOpen()` | 检查是否已打开 |
| `ReadFrame(frame)` | 读取一帧 H.264 数据 |
| `GetVideoInfo()` | 获取视频信息（包含 extradata） |
| `Seek(timestamp_ms)` | 跳转到指定时间 |

## VideoWriter - 视频写入器

### 基本用法

```cpp
#include "video_writer.h"
using namespace VideoCodec;

// 从输入文件获取参数
VideoReader reader;
reader.Open("input.mp4");
VideoInfo info = reader.GetVideoInfo();

// 创建输出文件（复制输入文件的参数）
VideoWriter writer;
VideoWriterParams params = VideoWriterParams::FromVideoInfo(info);

if (writer.Create("output.mp4", params)) {
    EncodedFrame frame;
    while (reader.ReadFrame(frame)) {
        writer.WriteFrame(frame);
    }
    writer.Close();
}
```

### 主要接口

| 接口 | 说明 |
|------|------|
| `Create(filepath, params)` | 创建输出文件 |
| `Close()` | 关闭文件，完成封装 |
| `IsOpen()` | 检查是否已创建 |
| `WriteFrame(frame)` | 写入一帧 H.264 数据 |
| `WriteH264Data(data, size, pts, is_key)` | 写入原始 H.264 数据 |
| `GetFrameCount()` | 获取已写入帧数 |

## 编译

```bash
cd VideoCodec
make all
```

## 测试

```bash
# 运行测试（读取 + 写入 + 验证）
make test

# 手动测试
./build/test_codec <input.mp4> [output.mp4]
```

## 与网络传输集成

```cpp
// 发送端
VideoReader reader;
reader.Open("input.mp4");

EncodedFrame frame;
while (reader.ReadFrame(frame)) {
    // 根据帧类型设置不同的传输参数
    if (frame.type == FrameType::I_FRAME) {
        // I 帧：小符号、高冗余（30%）
        send_with_high_protection(frame.data);
    } else {
        // P/B 帧：大符号、标准冗余（10%）
        send_with_standard_protection(frame.data);
    }
}

// 接收端
VideoWriter writer;
VideoWriterParams params;
params.width = 1920;
params.height = 1080;
// ... 设置其他参数
writer.Create("output.mp4", params);

while (receive_frame()) {
    EncodedFrame frame;
    frame.data = received_data;
    frame.pts = timestamp;
    frame.is_key_frame = is_key;
    frame.type = frame_type;
    
    writer.WriteFrame(frame);
}
writer.Close();
```

## 注意事项

1. **extradata**: VideoWriter 需要输入文件的 extradata（SPS/PPS），建议通过 `VideoWriterParams::FromVideoInfo()` 创建参数

2. **时间戳**: 使用毫秒时间戳，内部自动转换为流的 time_base

3. **帧类型检测**: VideoReader 通过 NAL 单元类型自动检测 I/P/B 帧

4. **B 帧处理**: 当前实现使用单调递增的 DTS 处理 B 帧，适合传输场景

## 历史变更

- **2026-03-10**: 重构为 H.264 NAL 透传模式
  - 移除 YUV 编解码（旧版 VideoEncoder/VideoDecoder）
  - 新增 VideoReader/VideoWriter 直接处理 H.264 数据
  - 支持 I/P/B 帧类型识别
  - 支持 extradata（SPS/PPS）复制
