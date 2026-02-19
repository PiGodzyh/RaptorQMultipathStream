/**
 * VideoEncoder - 视频编码器
 * 从 MP4 文件读取视频并提取帧
 * 
 * 使用示例:
 *   VideoEncoder encoder;
 *   if (encoder.Open("input.mp4")) {
 *       VideoInfo info = encoder.GetVideoInfo();
 *       VideoFrame frame;
 *       while (encoder.ReadFrame(frame)) {
 *           // 处理帧
 *       }
 *       encoder.Close();
 *   }
 */

#pragma once

#include "video_codec.h"
#include <functional>

// 前向声明 FFmpeg 结构
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace VideoCodec {

// 帧读取回调
using FrameCallback = std::function<bool(const VideoFrame& frame)>;

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    // 禁止拷贝，允许移动
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    /**
     * 打开视频文件
     * @param filepath 视频文件路径
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
     * 读取一帧
     * @param frame 输出帧结构（内部数据由 FFmpeg 管理，不要手动释放）
     * @return 是否成功读取，false 表示结束或错误
     */
    bool ReadFrame(VideoFrame& frame);

    /**
     * 使用回调读取所有帧
     * @param callback 回调函数，返回 false 停止读取
     * @return 读取的帧数
     */
    int ReadAllFrames(FrameCallback callback);

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

private:
    bool is_open_;
    ErrorCode last_error_;
    VideoInfo video_info_;

    // FFmpeg 上下文
    AVFormatContext* fmt_ctx_;
    AVCodecContext* codec_ctx_;
    AVFrame* av_frame_;
    AVPacket* packet_;
    SwsContext* sws_ctx_;
    
    // 视频流索引
    int video_stream_index_;
    
    // 当前时间戳
    int64_t current_pts_;
    
    // 帧率转换用
    AVFrame* rgb_frame_;
    
    // 内部转换
    bool ConvertFrame(AVFrame* src_frame, VideoFrame& dst_frame);
    
    // 初始化转换上下文
    bool InitSwsContext(int src_width, int src_height, int src_format);
};

} // namespace VideoCodec
