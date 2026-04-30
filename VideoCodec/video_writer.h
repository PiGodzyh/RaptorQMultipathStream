/**
 * video_writer.h - 视频写入器
 * 
 * 将H.264编码数据（NAL单元）写入MP4等容器文件，不解码
 * 
 * 使用示例:
 *   VideoWriter writer;
 *   VideoParams params;
 *   params.width = 1920;
 *   params.height = 1080;
 *   
 *   if (writer.Create("output.mp4", params)) {
 *       // 从网络接收或读取的H.264数据
 *       EncodedFrame frame;
 *       frame.data = h264_data;
 *       frame.pts = timestamp;
 *       frame.is_key_frame = is_key;
 *       frame.type = frame_type;
 *       
 *       writer.WriteFrame(frame);
 *       writer.Close();
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

class VideoWriter {
public:
    VideoWriter();
    ~VideoWriter();

    // 禁止拷贝，允许移动
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;
    VideoWriter(VideoWriter&&) noexcept;
    VideoWriter& operator=(VideoWriter&&) noexcept;

    /**
     * 创建输出视频文件
     * @param filepath 输出文件路径（支持MP4等格式）
     * @param params 视频参数（宽度、高度、帧率等）
     * @return 是否成功
     */
    bool Create(const std::string& filepath, const VideoParams& params);

    /**
     * 关闭文件，完成封装
     */
    void Close();

    /**
     * 检查是否已创建
     */
    bool IsOpen() const { return is_open_; }

    /**
     * 写入一帧H.264编码数据
     * @param frame 编码帧（包含NAL单元数据）
     * @return 是否成功
     */
    bool WriteFrame(const EncodedFrame& frame);

    /**
     * 写入原始H.264数据（简化接口）
     * @param data H.264 NAL单元数据
     * @param size 数据大小
     * @param pts 显示时间戳（毫秒）
     * @param is_key_frame 是否关键帧
     * @return 是否成功
     */
    bool WriteH264Data(const uint8_t* data, size_t size, 
                       int64_t pts, bool is_key_frame);

    /**
     * 获取视频参数
     */
    VideoParams GetVideoParams() const { return video_params_; }

    /**
     * 获取已写入帧数
     */
    int64_t GetFrameCount() const { return frame_count_; }

    /**
     * 刷新写入器，确保所有数据写入文件
     */
    bool Flush();

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
    VideoParams video_params_;
    int64_t frame_count_;
    
    // FFmpeg上下文
    AVFormatContext* fmt_ctx_;
    AVStream* video_stream_;
    AVPacket* packet_;
    std::vector<uint8_t> extradata_;  // 保存的SPS/PPS数据
    
    // Time base 转换（ffmpeg 可能调整 time_base）
    int orig_tb_num_, orig_tb_den_;     // 原始 time_base = {fps_den, fps_num}
    int stream_tb_num_, stream_tb_den_; // avformat_write_header 后的实际 time_base
    
    // 内部方法
    bool InitOutput(const std::string& filepath);
    bool WritePacket(const uint8_t* data, size_t size, 
                     int64_t pts, bool is_key_frame);
    int64_t ConvertMsToPts(int64_t ms) const;
};

} // namespace VideoCodec
