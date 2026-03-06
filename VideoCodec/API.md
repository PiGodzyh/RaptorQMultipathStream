# VideoCodec API 说明

## 概述

VideoCodec 模块提供基于 FFmpeg 的视频编解码功能，支持 MP4 格式。

- **VideoEncoder**: 视频读取器，从 MP4 文件解码并提取帧
- **VideoDecoder**: 视频写入器，将帧编码为 MP4 文件

## 依赖

- FFmpeg 开发库（至少包含 `libavformat`、`libavcodec`、`libavutil`、`libswscale`）
- 已在 FFmpeg 4.x 环境下测试（使用新版 API，无需 `av_register_all()`）

## 数据结构

### VideoFrame - 视频帧
```cpp
struct VideoFrame {
    uint8_t* data[4];       // YUV 数据指针 (Y, U, V, 保留)
    int linesize[4];        // 每行字节数
    int width;              // 帧宽度
    int height;             // 帧高度
    int64_t pts;            // 时间戳
    int64_t dts;            // 解码时间戳
    bool is_key_frame;      // 是否关键帧
};
```

### VideoInfo - 视频信息
```cpp
struct VideoInfo {
    std::string format_name;    // 格式名称
    std::string codec_name;     // 编码器名称
    int width;                  // 视频宽度
    int height;                 // 视频高度
    int fps_num;                // 帧率分子
    int fps_den;                // 帧率分母
    int64_t duration_ms;        // 时长 (毫秒)
    int64_t bitrate;            // 比特率
    int64_t frame_count;        // 总帧数
    std::string pixel_format;   // 像素格式
};
```

### EncodeParams - 编码参数
```cpp
struct EncodeParams {
    int width = 1920;               // 视频宽度
    int height = 1080;              // 视频高度
    int fps_num = 30;               // 帧率分子
    int fps_den = 1;                // 帧率分母
    int64_t bitrate = 2000000;      // 比特率 (bps)
    std::string codec_name;         // 编码器名称
    std::string pixel_format;       // 像素格式
};
```

## VideoEncoder - 视频读取接口

用于从 MP4 文件读取视频帧。

### 基本用法
```cpp
#include "video_encoder.h"
using namespace VideoCodec;

VideoEncoder encoder;
if (encoder.Open("input.mp4")) {
    // 获取视频信息
    VideoInfo info = encoder.GetVideoInfo();
    std::cout << "分辨率: " << info.width << "x" << info.height << std::endl;
    
    // 逐帧读取
    VideoFrame frame;
    while (encoder.ReadFrame(frame)) {
        // frame.data[0] = Y 平面
        // frame.data[1] = U 平面
        // frame.data[2] = V 平面
        // 处理帧...
    }
    
    encoder.Close();
}
```

### 使用回调读取
```cpp
encoder.ReadAllFrames([](const VideoFrame& frame) -> bool {
    // 处理帧
    // 返回 true 继续，false 停止
    return true;
});
```

### 主要接口

| 接口 | 说明 |
|------|------|
| `Open(filepath)` | 打开视频文件 |
| `Close()` | 关闭文件 |
| `IsOpen()` | 检查是否已打开 |
| `ReadFrame(frame)` | 读取一帧 |
| `ReadAllFrames(callback)` | 使用回调读取所有帧 |
| `GetVideoInfo()` | 获取视频信息 |
| `Seek(timestamp_ms)` | 跳转到指定时间 |
| `GetCurrentTimestamp()` | 获取当前时间戳 |

## VideoDecoder - 视频写入接口

用于将帧编码为 MP4 文件。

### 基本用法
```cpp
#include "video_decoder.h"
using namespace VideoCodec;

// 设置编码参数
EncodeParams params;
params.width = 640;
params.height = 480;
params.fps_num = 30;
params.bitrate = 2000000;

VideoDecoder decoder;
if (decoder.Create("output.mp4", params)) {
    // 准备 YUV 数据
    std::vector<uint8_t> y_data(width * height);
    std::vector<uint8_t> u_data(width * height / 4);
    std::vector<uint8_t> v_data(width * height / 4);
    
    // 填充 YUV 数据...
    
    // 写入帧
    for (int i = 0; i < num_frames; i++) {
        decoder.WriteYUVData(y_data.data(), u_data.data(), v_data.data(), i);
    }
    
    decoder.Close();
}
```

### 主要接口

| 接口 | 说明 |
|------|------|
| `Create(filepath, params)` | 创建输出文件 |
| `Close()` | 关闭文件，完成编码 |
| `IsOpen()` | 检查是否已创建 |
| `WriteFrame(frame)` | 写入一帧 |
| `WriteYUVData(y, u, v, pts)` | 写入 YUV 数据 |
| `GetFrameTemplate()` | 获取帧模板 |
| `Flush()` | 刷新编码器 |
| `GetFrameCount()` | 获取已写入帧数 |

## 后续集成建议

### 与网络传输集成

可以将 VideoEncoder 读取的帧通过网络传输，然后在接收端使用 VideoDecoder 写入文件：

```cpp
// 发送端
VideoEncoder encoder;
encoder.Open("input.mp4");

VideoFrame frame;
while (encoder.ReadFrame(frame)) {
    // 将 YUV 数据打包发送到网络
    send_yuv_data(frame.data[0], frame.linesize[0] * frame.height);  // Y
    send_yuv_data(frame.data[1], frame.linesize[1] * frame.height/2); // U
    send_yuv_data(frame.data[2], frame.linesize[2] * frame.height/2); // V
}

// 接收端
VideoDecoder decoder;
decoder.Create("output.mp4", params);

while (receive_frame()) {
    // 接收 YUV 数据
    decoder.WriteYUVData(y_data, u_data, v_data, pts);
}
decoder.Close();
```

## 编译

```bash
cd VideoCodec
make all
```

## 测试

```bash
# 测试视频读取
make test-encode

# 测试视频生成
make test-decode

# 运行联合测试，会将原视频编码再解码
make test-transcode

# 运行所有测试
make test
```
