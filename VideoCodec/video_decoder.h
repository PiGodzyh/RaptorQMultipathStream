/**
 * VideoDecoder - 视频解码器（编码输出）
 * 将视频帧编码写入 MP4 文件
 * 
 * 使用示例:
 *   VideoDecoder decoder;
 *   EncodeParams params;
 *   params.width = 1920;
 *   params.height = 1080;
 *   params.fps_num = 30;
 *   
 *   if (decoder.Create("output.mp4", params)) {
 *       // 获取帧模板
 *       VideoFrame frame_template = decoder.GetFrameTemplate();
 *       
 *       // 填充帧数据并写入
 *       // ... 填充 frame 数据 ...
 *       decoder.WriteFrame(frame);
 *       
 *       decoder.Close();
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
struct AVStream;
struct AVOutputFormat;

namespace VideoCodec {

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    // 禁止拷贝，允许移动
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    /**
     * 创建输出视频文件
     * @param filepath 输出文件路径
     * @param params 编码参数
     * @return 是否成功
     */
    bool Create(const std::string& filepath, const EncodeParams& params);

    /**
     * 关闭文件，完成编码
     */
    void Close();

    /**
     * 检查是否已创建
     */
    bool IsOpen() const { return is_open_; }

    /**
     * 写入一帧
     * @param frame 视频帧（必须是 YUV420P 格式）
     * @return 是否成功
     */
    bool WriteFrame(const VideoFrame& frame);

    /**
     * 写入原始 YUV 数据
     * @param y_data Y 平面数据
     * @param u_data U 平面数据
     * @param v_data V 平面数据
     * @param pts 时间戳
     * @return 是否成功
     */
    bool WriteYUVData(const uint8_t* y_data, const uint8_t* u_data, const uint8_t* v_data,
                      int64_t pts);

    /**
     * 获取帧模板（用于创建新帧）
     * @return 帧模板（包含正确的宽高和格式信息）
     */
    VideoFrame GetFrameTemplate() const;

    /**
     * 获取编码参数
     */
    EncodeParams GetEncodeParams() const { return encode_params_; }

    /**
     * 刷新编码器，写入缓存帧
     */
    bool Flush();

    /**
     * 获取已写入帧数
     */
    int64_t GetFrameCount() const { return frame_count_; }

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
    EncodeParams encode_params_;
    int64_t frame_count_;
    
    // FFmpeg 上下文
    AVFormatContext* fmt_ctx_;
    AVCodecContext* codec_ctx_;
    AVFrame* av_frame_;
    AVPacket* packet_;
    SwsContext* sws_ctx_;
    AVStream* video_stream_;
    
    // 临时帧（用于格式转换）
    AVFrame* tmp_frame_;
    uint8_t* tmp_buffer_;
    
    // 下一帧的 pts
    int64_t next_pts_;
    
    // 编码并写入包
    bool EncodeAndWriteFrame(AVFrame* frame);
    
    // 写入包
    bool WritePacket(AVPacket* packet);
    
    // 初始化临时帧
    bool InitTmpFrame();
};

} // namespace VideoCodec
