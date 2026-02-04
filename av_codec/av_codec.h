/**
 * @file av_codec.h
 * @brief 音视频编解码模块
 * 
 * 使用 FFmpeg 实现音视频的编解码功能：
 * - 从文件读取音视频数据包
 * - 将数据包写入输出文件
 * 
 * 本模块只负责音视频编解码，不涉及网络传输和FEC编码
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>

// FFmpeg headers
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/timestamp.h>
}

namespace AVCodecModule {

/**
 * 媒体帧类型
 */
enum class FrameType : uint8_t {
    UNKNOWN = 0,
    VIDEO = 1,
    AUDIO = 2,
    END_OF_STREAM = 255  // 传输结束标记
};

/**
 * 媒体信息
 */
struct MediaInfo {
    // 视频信息
    bool has_video = false;
    int video_width = 0;
    int video_height = 0;
    AVPixelFormat video_pixel_format = AV_PIX_FMT_NONE;
    AVRational video_time_base = {0, 1};
    int video_stream_index = -1;
    AVCodecID video_codec_id = AV_CODEC_ID_NONE;
    uint32_t video_codec_tag = 0;  // 编码器标签（如 hvc1, hev1）
    
    // 音频信息
    bool has_audio = false;
    int audio_sample_rate = 0;
    int audio_channels = 0;
    AVSampleFormat audio_sample_format = AV_SAMPLE_FMT_NONE;
    AVRational audio_time_base = {0, 1};
    int audio_stream_index = -1;
    AVCodecID audio_codec_id = AV_CODEC_ID_NONE;
    uint32_t audio_codec_tag = 0;  // 编码器标签
    
    // 通用信息
    int64_t duration = 0;         // 持续时间（微秒）
    int64_t bit_rate = 0;         // 比特率
};

/**
 * 媒体数据包 - 用于传输的数据单元
 */
struct MediaPacket {
    FrameType type = FrameType::UNKNOWN;
    uint32_t index = 0;           // 包序号
    int64_t pts = 0;              // 显示时间戳
    int64_t dts = 0;              // 解码时间戳
    bool is_key_frame = false;    // 是否关键帧
    std::vector<uint8_t> data;    // 数据
    
    // 序列化为字节流（用于传输）
    std::vector<uint8_t> serialize() const;
    
    // 从字节流反序列化
    static bool deserialize(const uint8_t* data, size_t size, MediaPacket& packet);
    static bool deserialize(const std::vector<uint8_t>& data, MediaPacket& packet);
};

/**
 * 媒体头信息 - 用于初始化解码器
 */
struct MediaHeader {
    static const uint32_t MAGIC = 0x4D484452;  // "MHDR"
    
    MediaInfo info;
    std::vector<uint8_t> video_extradata;  // 视频额外数据（如SPS/PPS）
    std::vector<uint8_t> audio_extradata;  // 音频额外数据
    
    // 序列化为字节流
    std::vector<uint8_t> serialize() const;
    
    // 从字节流反序列化
    static bool deserialize(const uint8_t* data, size_t size, MediaHeader& header);
    static bool deserialize(const std::vector<uint8_t>& data, MediaHeader& header);
};

/**
 * 媒体文件读取器
 * 从媒体文件读取数据包
 */
class MediaReader {
public:
    MediaReader();
    ~MediaReader();
    
    // 禁止拷贝
    MediaReader(const MediaReader&) = delete;
    MediaReader& operator=(const MediaReader&) = delete;
    
    /**
     * 打开媒体文件
     * @param filename 文件路径
     * @return 成功返回true
     */
    bool open(const std::string& filename);
    
    /**
     * 关闭文件
     */
    void close();
    
    /**
     * 是否已打开
     */
    bool isOpen() const { return format_ctx_ != nullptr; }
    
    /**
     * 获取媒体信息
     */
    const MediaInfo& getMediaInfo() const { return media_info_; }
    
    /**
     * 获取媒体头（用于初始化写入器）
     */
    MediaHeader getMediaHeader() const;
    
    /**
     * 读取下一个数据包
     * @param packet 输出数据包
     * @return 成功返回true，文件结束或错误返回false
     */
    bool readPacket(MediaPacket& packet);
    
    /**
     * 跳转到指定时间
     * @param timestamp_us 时间戳（微秒）
     */
    bool seek(int64_t timestamp_us);
    
    /**
     * 获取已读取的包数
     */
    uint64_t getPacketCount() const { return packet_count_; }
    
private:
    AVFormatContext* format_ctx_ = nullptr;
    MediaInfo media_info_;
    uint64_t packet_count_ = 0;
};

/**
 * 媒体文件写入器
 * 将数据包写入媒体文件
 */
class MediaWriter {
public:
    MediaWriter();
    ~MediaWriter();
    
    // 禁止拷贝
    MediaWriter(const MediaWriter&) = delete;
    MediaWriter& operator=(const MediaWriter&) = delete;
    
    /**
     * 使用媒体头初始化
     * @param header 媒体头
     * @return 成功返回true
     */
    bool initialize(const MediaHeader& header);
    
    /**
     * 打开输出文件
     * @param filename 输出文件路径
     * @return 成功返回true
     */
    bool open(const std::string& filename);
    
    /**
     * 关闭输出文件
     */
    void close();
    
    /**
     * 是否已打开
     */
    bool isOpen() const { return output_ctx_ != nullptr && header_written_; }
    
    /**
     * 是否已初始化
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * 写入数据包
     * @param packet 数据包
     * @return 成功返回true
     */
    bool writePacket(const MediaPacket& packet);
    
    /**
     * 完成写入（刷新缓冲区，写入文件尾）
     */
    bool finalize();
    
    /**
     * 获取已写入的包数
     */
    uint64_t getPacketCount() const { return packet_count_; }
    
    /**
     * 获取视频包数
     */
    uint64_t getVideoPacketCount() const { return video_packet_count_; }
    
    /**
     * 获取音频包数
     */
    uint64_t getAudioPacketCount() const { return audio_packet_count_; }
    
    /**
     * 获取关键帧数
     */
    uint64_t getKeyFrameCount() const { return key_frame_count_; }
    
    /**
     * 设置缓冲模式（默认开启）
     * 开启时包会缓冲到 finalize() 时才写入
     */
    void setBufferingMode(bool enabled) { buffering_mode_ = enabled; }
    
private:
    bool writePacketDirect(const MediaPacket& packet);
    AVFormatContext* output_ctx_ = nullptr;
    AVStream* video_stream_ = nullptr;
    AVStream* audio_stream_ = nullptr;
    
    MediaHeader header_;
    bool initialized_ = false;
    bool header_written_ = false;
    uint64_t packet_count_ = 0;
    uint64_t video_packet_count_ = 0;
    uint64_t audio_packet_count_ = 0;
    uint64_t key_frame_count_ = 0;
    
    // 包缓冲区（用于排序后写入）
    std::vector<MediaPacket> packet_buffer_;
    bool buffering_mode_ = true;  // 是否使用缓冲模式
};

} // namespace AVCodecModule
