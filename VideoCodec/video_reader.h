/**
 * video_reader.h - 视频读取器
 * 
 * 从MP4等容器文件读取H.264编码数据（NAL单元），不解码为YUV
 * 
 * 使用示例:
 *   VideoReader reader;
 *   if (reader.Open("input.mp4")) {
 *       VideoInfo info = reader.GetVideoInfo();
 *       EncodedFrame frame;
 *       while (reader.ReadFrame(frame)) {
 *           // frame.data 包含H.264 NAL单元
 *           // frame.is_key_frame 标识是否为I帧
 *           // frame.type 标识为I/P/B帧
 *       }
 *       reader.Close();
 *   }
 */

#pragma once

#include "video_codec.h"
#include <functional>

// FFmpeg前向声明
struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVStream;

namespace VideoCodec {

// 帧读取回调
using FrameReadCallback = std::function<bool(const EncodedFrame& frame)>;

class VideoReader {
public:
    VideoReader();
    ~VideoReader();

    // 禁止拷贝，允许移动
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;
    VideoReader(VideoReader&&) noexcept;
    VideoReader& operator=(VideoReader&&) noexcept;

    /**
     * 打开视频文件
     * @param filepath 视频文件路径（支持MP4等格式）
     * @return 是否成功
     */
    bool Open(const std::string& filepath);

    /**
     * 关闭文件，释放资源
     */
    void Close();

    /**
     * 检查是否已打开
     */
    bool IsOpen() const { return is_open_; }

    /**
     * 获取视频信息
     */
    VideoInfo GetVideoInfo() const { return video_info_; }

    /**
     * 读取一帧H.264编码数据
     * @param frame 输出编码帧结构（包含NAL单元数据）
     * @return 是否成功读取，false表示结束或错误
     */
    bool ReadFrame(EncodedFrame& frame);

    /**
     * 使用回调读取所有帧
     * @param callback 回调函数，返回 false 停止读取
     * @return 读取的帧数
     */
    int ReadAllFrames(FrameReadCallback callback);

    /**
     * 跳转到指定时间戳
     * @param timestamp_ms 目标时间戳（毫秒）
     * @return 是否成功
     */
    bool Seek(int64_t timestamp_ms);

    /**
     * 获取当前时间戳（毫秒）
     */
    int64_t GetCurrentTimestamp() const;

    /**
     * 获取最后一帧错误码
     */
    ErrorCode GetLastError() const { return last_error_; }

    /**
     * 获取最后一帧错误信息
     */
    std::string GetLastErrorString() const;

    /**
     * 获取当前GOP编号
     */
    uint32_t GetCurrentGopId() const { return current_gop_id_; }

private:
    bool is_open_;
    ErrorCode last_error_;
    VideoInfo video_info_;

    // FFmpeg上下文
    AVFormatContext* fmt_ctx_;
    AVCodecContext* codec_ctx_;
    AVPacket* packet_;
    AVStream* video_stream_;
    
    // 视频流索引
    int video_stream_index_;
    
    // 当前时间戳
    int64_t current_pts_;
    
    // GOP状态
    uint32_t current_gop_id_;
    uint32_t frame_in_gop_;
    
    // 内部方法
    bool InitStreams();
    FrameType DetectFrameType(const uint8_t* data, size_t size, bool is_key_frame);
    int64_t ConvertPtsToMs(int64_t pts) const;
};

} // namespace VideoCodec
