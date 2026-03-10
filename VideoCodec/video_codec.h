/**
 * video_codec.h - 视频编解码公共头文件
 * 
 * 修改记录：
 * - 2026-03-10: 重构为H.264 NAL透传模式，移除YUV编解码
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace VideoCodec {

// 帧类型
enum class FrameType : uint8_t {
    UNKNOWN = 0,
    I_FRAME = 1,    // 关键帧（IDR）
    P_FRAME = 2,    // 前向预测帧
    B_FRAME = 3,    // 双向预测帧
};

// 编码后的视频帧（H.264 NAL单元）
struct EncodedFrame {
    std::vector<uint8_t> data;      // H.264 NAL单元数据
    int64_t pts;                    // 显示时间戳（毫秒）
    int64_t dts;                    // 解码时间戳（毫秒）
    bool is_key_frame;              // 是否关键帧
    FrameType type;                 // 帧类型
    uint32_t gop_id;                // GOP编号
    
    EncodedFrame() 
        : pts(0), dts(0)
        , is_key_frame(false)
        , type(FrameType::UNKNOWN)
        , gop_id(0) {}
};

// 视频信息结构
struct VideoInfo {
    std::string format_name;    // 格式名称（如 "mov,mp4,m4a"）
    std::string codec_name;     // 编码器名称（如 "h264"）
    int width;                  // 视频宽度
    int height;                 // 视频高度
    int fps_num;                // 帧率分子
    int fps_den;                // 帧率分母
    int64_t duration_ms;        // 时长 (毫秒)
    int64_t bitrate;            // 比特率
    int64_t frame_count;        // 总帧数 (可能为0表示未知)
    int gop_size;               // GOP大小
    std::vector<uint8_t> extradata;  // H.264的SPS/PPS数据
    
    VideoInfo() 
        : width(0), height(0)
        , fps_num(0), fps_den(1)
        , duration_ms(0), bitrate(0)
        , frame_count(0), gop_size(0) {}
};

// 视频编码参数（用于创建输出文件）
struct VideoWriterParams {
    int width;                  // 视频宽度
    int height;                 // 视频高度
    int fps_num;                // 帧率分子
    int fps_den;                // 帧率分母
    int64_t bitrate;            // 比特率 (bps)
    int gop_size;               // GOP大小
    std::string codec_name;     // 编码器名称
    std::vector<uint8_t> extradata;  // 从输入文件复制的SPS/PPS
    
    VideoWriterParams() 
        : width(1920), height(1080)
        , fps_num(30), fps_den(1)
        , bitrate(2000000)
        , gop_size(12)
        , codec_name("h264") {}
    
    // 从VideoInfo创建
    static VideoWriterParams FromVideoInfo(const VideoInfo& info) {
        VideoWriterParams params;
        params.width = info.width;
        params.height = info.height;
        params.fps_num = info.fps_num;
        params.fps_den = info.fps_den;
        params.bitrate = info.bitrate;
        params.gop_size = info.gop_size;
        params.codec_name = info.codec_name;
        params.extradata = info.extradata;
        return params;
    }
};

// 保持向后兼容
using VideoParams = VideoWriterParams;

// 错误码
enum class ErrorCode {
    SUCCESS = 0,
    FILE_NOT_FOUND = -1,
    INVALID_FORMAT = -2,
    CODEC_NOT_FOUND = -3,
    STREAM_NOT_FOUND = -4,
    ALLOC_FAILED = -5,
    READ_FAILED = -6,
    WRITE_FAILED = -7,
    HEADER_WRITE_FAILED = -8,
    UNKNOWN_ERROR = -100
};

// 获取错误信息
const char* GetErrorString(ErrorCode code);

// 判断帧类型字符串
const char* FrameTypeToString(FrameType type);

} // namespace VideoCodec
