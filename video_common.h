/**
 * video_common.h - 视频传输公共定义
 * 
 * 定义视频帧类型、视频数据包头部结构、传输参数等
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace VideoTransmit {

// 视频帧类型
enum class FrameType : uint8_t {
    UNKNOWN = 0,
    I_FRAME = 1,    // 关键帧
    P_FRAME = 2,    // 前向预测帧
    B_FRAME = 3,    // 双向预测帧
    CONFIG = 4      // 视频配置信息
};

// 视频配置信息（在传输视频帧之前发送）
struct VideoConfig {
    uint32_t width;             // 视频宽度
    uint32_t height;            // 视频高度
    uint32_t fps_num;           // 帧率分子
    uint32_t fps_den;           // 帧率分母
    uint64_t bitrate;           // 比特率 (bps)
    uint32_t gop_size;          // GOP大小
    char codec_name[32];        // 编码器名称
    char pixel_format[32];      // 像素格式
    uint32_t extradata_size;    // extradata大小（SPS/PPS）
    // extradata 紧随其后（变长）
    
    VideoConfig() 
        : width(1920), height(1080)
        , fps_num(30), fps_den(1)
        , bitrate(2000000)
        , gop_size(12)
        , extradata_size(0) {
        std::memset(codec_name, 0, sizeof(codec_name));
        std::memset(pixel_format, 0, sizeof(pixel_format));
        std::strcpy(codec_name, "h264");
        std::strcpy(pixel_format, "yuv420p");
    }
} __attribute__((packed));

// 视频帧头部（在RaptorQ编码前添加）
struct VideoFrameHeader {
    // 基础信息
    FrameType frame_type;       // 帧类型
    uint8_t reserved;           // 保留字段
    uint16_t flags;             // 标志位
    
    // 时间信息
    int64_t pts;                // 显示时间戳
    int64_t dts;                // 解码时间戳
    
    // GOP信息
    uint32_t gop_id;            // GOP编号
    uint32_t frame_in_gop;      // 在GOP中的帧序号
    
    // 帧数据信息
    uint32_t frame_size;        // 原始帧数据大小
    uint32_t frame_seq;         // 全局帧序列号
    
    // Source Block信息
    uint32_t source_block_id;   // Source Block ID
    uint32_t total_symbols;     // 该帧的总符号数
    
    VideoFrameHeader()
        : frame_type(FrameType::UNKNOWN)
        , reserved(0)
        , flags(0)
        , pts(0), dts(0)
        , gop_id(0)
        , frame_in_gop(0)
        , frame_size(0)
        , frame_seq(0)
        , source_block_id(0)
        , total_symbols(0) {}
} __attribute__((packed));

// 标志位定义
enum FrameFlags : uint16_t {
    FLAG_NONE = 0,
    FLAG_FIRST_SYMBOL = 1 << 0,     // 这是帧的第一个符号
    FLAG_LAST_SYMBOL = 1 << 1,      // 这是帧的最后一个符号
    FLAG_CONFIG_FRAME = 1 << 2,     // 这是配置帧
};

// I帧和P/B帧的差异化传输参数
struct FrameTransmitParams {
    uint16_t symbol_size;       // 符号大小
    float repair_ratio;         // 冗余比例
    uint32_t max_block_size;    // 最大Source Block大小
    bool high_priority;         // 是否高优先级
    uint32_t send_interval_us;  // 发送间隔（微秒）
    
    // I帧默认参数：小符号、高冗余、高优先级
    static FrameTransmitParams IFrameParams() {
        FrameTransmitParams params;
        params.symbol_size = 1024;          // 更小的符号
        params.repair_ratio = 0.5f;         // 50%冗余，抗丢包
        params.max_block_size = 64 * 1024;  // 64KB，I帧单独成块
        params.high_priority = true;
        params.send_interval_us = 10000;    // 10ms发送间隔，避免UDP缓冲区溢出
        return params;
    }
    
    // P/B帧默认参数：标准冗余、平滑发送
    static FrameTransmitParams PBFrameParams() {
        FrameTransmitParams params;
        params.symbol_size = 1024;          // 统一符号大小
        params.repair_ratio = 0.3f;         // 30%冗余
        params.max_block_size = 128 * 1024; // 128KB，多帧聚合
        params.high_priority = false;
        params.send_interval_us = 10000;    // 10ms发送间隔，避免UDP缓冲区溢出
        return params;
    }
};

// 根据帧类型获取传输参数
inline FrameTransmitParams GetTransmitParams(FrameType type) {
    switch (type) {
        case FrameType::I_FRAME:
            return FrameTransmitParams::IFrameParams();
        case FrameType::P_FRAME:
        case FrameType::B_FRAME:
            return FrameTransmitParams::PBFrameParams();
        default:
            return FrameTransmitParams::PBFrameParams();
    }
}

// 判断帧类型是否是关键帧
inline bool IsKeyFrame(FrameType type) {
    return type == FrameType::I_FRAME;
}

// 视频传输统计
struct VideoTransmitStats {
    // 发送端统计
    uint64_t frames_sent;           // 发送帧数
    uint64_t i_frames_sent;         // I帧数
    uint64_t p_frames_sent;         // P帧数
    uint64_t b_frames_sent;         // B帧数
    uint64_t bytes_sent;            // 发送字节数
    uint64_t symbols_sent;          // 发送符号数
    uint64_t source_blocks_sent;    // Source Block数
    
    // 接收端统计
    uint64_t frames_received;       // 接收帧数
    uint64_t i_frames_received;     // I帧数
    uint64_t p_frames_received;     // P帧数
    uint64_t b_frames_received;     // B帧数
    uint64_t bytes_received;        // 接收字节数
    
    VideoTransmitStats()
        : frames_sent(0)
        , i_frames_sent(0)
        , p_frames_sent(0)
        , b_frames_sent(0)
        , bytes_sent(0)
        , symbols_sent(0)
        , source_blocks_sent(0)
        , frames_received(0)
        , i_frames_received(0)
        , p_frames_received(0)
        , b_frames_received(0)
        , bytes_received(0) {}
};

// 带完整数据的视频配置（用于传输）
struct VideoConfigPacket {
    VideoConfig config;
    std::vector<uint8_t> extradata;
    
    // 序列化为字节流
    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> data;
        size_t total_size = sizeof(VideoConfig) + extradata.size();
        data.resize(total_size);
        
        // 复制config
        memcpy(data.data(), &config, sizeof(VideoConfig));
        
        // 更新extradata_size
        VideoConfig* cfg = reinterpret_cast<VideoConfig*>(data.data());
        cfg->extradata_size = extradata.size();
        
        // 复制extradata
        if (!extradata.empty()) {
            memcpy(data.data() + sizeof(VideoConfig), extradata.data(), extradata.size());
        }
        
        return data;
    }
    
    // 从字节流反序列化
    bool Deserialize(const uint8_t* data, size_t size) {
        if (size < sizeof(VideoConfig)) {
            return false;
        }
        
        memcpy(&config, data, sizeof(VideoConfig));
        
        if (config.extradata_size > 0) {
            if (size < sizeof(VideoConfig) + config.extradata_size) {
                return false;
            }
            extradata.resize(config.extradata_size);
            memcpy(extradata.data(), data + sizeof(VideoConfig), config.extradata_size);
        }
        
        return true;
    }
};

} // namespace VideoTransmit
