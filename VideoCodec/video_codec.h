/**
 * VideoCodec - 视频编解码公共头文件
 * 定义公共数据结构、枚举和常量
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace VideoCodec {

// 视频帧结构
struct VideoFrame {
    uint8_t* data[4];           // YUV/RGB 数据指针
    int linesize[4];            // 每行字节数
    int width;                  // 帧宽度
    int height;                 // 帧高度
    int64_t pts;                // 时间戳
    int64_t dts;                // 解码时间戳
    bool is_key_frame;          // 是否关键帧
    
    VideoFrame() : width(0), height(0), pts(0), dts(0), is_key_frame(false) {
        for (int i = 0; i < 4; i++) {
            data[i] = nullptr;
            linesize[i] = 0;
        }
    }
};

// 视频编码参数
struct EncodeParams {
    int width;                  // 视频宽度
    int height;                 // 视频高度
    int fps_num;                // 帧率分子
    int fps_den;                // 帧率分母
    int64_t bitrate;            // 比特率 (bps)
    std::string codec_name;     // 编码器名称 (如 "libx264", "h264")
    std::string pixel_format;   // 像素格式 (如 "yuv420p", "rgb24")
    
    EncodeParams() 
        : width(1920), height(1080), 
          fps_num(30), fps_den(1), 
          bitrate(2000000),       // 2 Mbps
          codec_name("libx264"),
          pixel_format("yuv420p") {}
};

// 视频信息结构
struct VideoInfo {
    std::string format_name;    // 格式名称
    std::string codec_name;     // 编码器名称
    int width;                  // 视频宽度
    int height;                 // 视频高度
    int fps_num;                // 帧率分子
    int fps_den;                // 帧率分母
    int64_t duration_ms;        // 时长 (毫秒)
    int64_t bitrate;            // 比特率
    int64_t frame_count;        // 总帧数 (可能为0表示未知)
    std::string pixel_format;   // 像素格式
    
    VideoInfo() 
        : width(0), height(0), 
          fps_num(0), fps_den(1), 
          duration_ms(0), bitrate(0), frame_count(0) {}
};

// 错误码
enum class ErrorCode {
    SUCCESS = 0,
    FILE_NOT_FOUND = -1,
    INVALID_FORMAT = -2,
    CODEC_NOT_FOUND = -3,
    DECODER_OPEN_FAILED = -4,
    ENCODER_OPEN_FAILED = -5,
    ALLOC_FAILED = -6,
    READ_FAILED = -7,
    WRITE_FAILED = -8,
    FLUSH_FAILED = -9,
    UNKNOWN_ERROR = -100
};

// 获取错误信息
const char* GetErrorString(ErrorCode code);

} // namespace VideoCodec
