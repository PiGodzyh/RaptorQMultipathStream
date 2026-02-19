/**
 * VideoEncoder 实现
 */

#include "video_encoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

#include <iostream>
#include <cstring>

namespace VideoCodec {

const char* GetErrorString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "Success";
        case ErrorCode::FILE_NOT_FOUND: return "File not found";
        case ErrorCode::INVALID_FORMAT: return "Invalid format";
        case ErrorCode::CODEC_NOT_FOUND: return "Codec not found";
        case ErrorCode::DECODER_OPEN_FAILED: return "Decoder open failed";
        case ErrorCode::ENCODER_OPEN_FAILED: return "Encoder open failed";
        case ErrorCode::ALLOC_FAILED: return "Memory allocation failed";
        case ErrorCode::READ_FAILED: return "Read failed";
        case ErrorCode::WRITE_FAILED: return "Write failed";
        case ErrorCode::FLUSH_FAILED: return "Flush failed";
        case ErrorCode::UNKNOWN_ERROR: return "Unknown error";
        default: return "Invalid error code";
    }
}

VideoEncoder::VideoEncoder()
    : is_open_(false)
    , last_error_(ErrorCode::SUCCESS)
    , fmt_ctx_(nullptr)
    , codec_ctx_(nullptr)
    , av_frame_(nullptr)
    , packet_(nullptr)
    , sws_ctx_(nullptr)
    , video_stream_index_(-1)
    , current_pts_(0)
    , rgb_frame_(nullptr) {
}

VideoEncoder::~VideoEncoder() {
    Close();
}

bool VideoEncoder::Open(const std::string& filepath) {
    if (is_open_) {
        Close();
    }

    // 注册所有格式和解码器
    static bool registered = false;
    if (!registered) {
        av_register_all();
        registered = true;
    }

    last_error_ = ErrorCode::SUCCESS;

    // 打开输入文件
    int ret = avformat_open_input(&fmt_ctx_, filepath.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "Cannot open input file: " << filepath << std::endl;
        last_error_ = ErrorCode::FILE_NOT_FOUND;
        return false;
    }

    // 获取流信息
    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "Cannot find stream information" << std::endl;
        last_error_ = ErrorCode::INVALID_FORMAT;
        Close();
        return false;
    }

    // 查找视频流
    video_stream_index_ = -1;
    for (unsigned int i = 0; i < fmt_ctx_->nb_streams; i++) {
        if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index_ = i;
            break;
        }
    }

    if (video_stream_index_ == -1) {
        std::cerr << "Cannot find video stream" << std::endl;
        last_error_ = ErrorCode::INVALID_FORMAT;
        Close();
        return false;
    }

    AVStream* stream = fmt_ctx_->streams[video_stream_index_];
    AVCodecParameters* codecpar = stream->codecpar;

    // 查找解码器
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "Codec not found" << std::endl;
        last_error_ = ErrorCode::CODEC_NOT_FOUND;
        Close();
        return false;
    }

    // 创建解码器上下文
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "Cannot allocate codec context" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        Close();
        return false;
    }

    // 复制参数到解码器上下文
    ret = avcodec_parameters_to_context(codec_ctx_, codecpar);
    if (ret < 0) {
        std::cerr << "Cannot copy codec parameters to decoder context" << std::endl;
        last_error_ = ErrorCode::DECODER_OPEN_FAILED;
        Close();
        return false;
    }

    // 打开解码器
    ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        std::cerr << "Cannot open codec" << std::endl;
        last_error_ = ErrorCode::DECODER_OPEN_FAILED;
        Close();
        return false;
    }

    // 填充视频信息
    video_info_.format_name = fmt_ctx_->iformat->name;
    video_info_.codec_name = codec->name;
    video_info_.width = codec_ctx_->width;
    video_info_.height = codec_ctx_->height;
    
    // 计算帧率
    AVRational frame_rate = av_guess_frame_rate(fmt_ctx_, stream, nullptr);
    video_info_.fps_num = frame_rate.num;
    video_info_.fps_den = frame_rate.den;
    
    // 获取时长
    if (stream->duration != AV_NOPTS_VALUE) {
        video_info_.duration_ms = av_rescale_q(stream->duration, stream->time_base, {1, 1000});
    } else if (fmt_ctx_->duration != AV_NOPTS_VALUE) {
        video_info_.duration_ms = fmt_ctx_->duration / 1000; // AV_TIME_BASE is microseconds
    }
    
    video_info_.bitrate = codecpar->bit_rate;
    video_info_.frame_count = stream->nb_frames;
    
    const AVPixFmtDescriptor* pix_desc = av_pix_fmt_desc_get(codec_ctx_->pix_fmt);
    if (pix_desc) {
        video_info_.pixel_format = pix_desc->name;
    }

    // 分配帧和包
    av_frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!av_frame_ || !packet_) {
        std::cerr << "Cannot allocate frame or packet" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        Close();
        return false;
    }

    is_open_ = true;
    current_pts_ = 0;

    std::cout << "VideoEncoder: Opened " << filepath << std::endl;
    std::cout << "  Format: " << video_info_.format_name << std::endl;
    std::cout << "  Codec: " << video_info_.codec_name << std::endl;
    std::cout << "  Resolution: " << video_info_.width << "x" << video_info_.height << std::endl;
    std::cout << "  FPS: " << video_info_.fps_num << "/" << video_info_.fps_den << std::endl;
    std::cout << "  Duration: " << video_info_.duration_ms << " ms" << std::endl;

    return true;
}

void VideoEncoder::Close() {
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }

    if (rgb_frame_) {
        av_frame_free(&rgb_frame_);
        rgb_frame_ = nullptr;
    }

    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }

    if (av_frame_) {
        av_frame_free(&av_frame_);
        av_frame_ = nullptr;
    }

    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }

    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
        fmt_ctx_ = nullptr;
    }

    is_open_ = false;
    video_stream_index_ = -1;
    current_pts_ = 0;
}

bool VideoEncoder::ReadFrame(VideoFrame& frame) {
    if (!is_open_) {
        return false;
    }

    last_error_ = ErrorCode::SUCCESS;

    while (true) {
        int ret = av_read_frame(fmt_ctx_, packet_);
        
        if (ret < 0) {
            // 文件结束，尝试刷新解码器
            avcodec_send_packet(codec_ctx_, nullptr);
            ret = avcodec_receive_frame(codec_ctx_, av_frame_);
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                return false;
            }
        } else {
            if (packet_->stream_index != video_stream_index_) {
                av_packet_unref(packet_);
                continue;
            }

            // 发送包到解码器
            ret = avcodec_send_packet(codec_ctx_, packet_);
            av_packet_unref(packet_);
            
            if (ret < 0) {
                std::cerr << "Error sending packet to decoder" << std::endl;
                last_error_ = ErrorCode::READ_FAILED;
                continue;
            }

            // 接收解码后的帧
            ret = avcodec_receive_frame(codec_ctx_, av_frame_);
        }

        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret == AVERROR_EOF) {
            return false;
        } else if (ret < 0) {
            std::cerr << "Error during decoding" << std::endl;
            last_error_ = ErrorCode::READ_FAILED;
            return false;
        }

        // 成功获取帧
        frame.width = av_frame_->width;
        frame.height = av_frame_->height;
        frame.pts = av_frame_->pts;
        frame.dts = av_frame_->pkt_dts;
        frame.is_key_frame = av_frame_->key_frame;
        
        for (int i = 0; i < 4; i++) {
            frame.data[i] = av_frame_->data[i];
            frame.linesize[i] = av_frame_->linesize[i];
        }

        current_pts_ = frame.pts;
        return true;
    }
}

int VideoEncoder::ReadAllFrames(FrameCallback callback) {
    if (!is_open_ || !callback) {
        return 0;
    }

    int count = 0;
    VideoFrame frame;
    
    while (ReadFrame(frame)) {
        count++;
        if (!callback(frame)) {
            break;
        }
    }
    
    return count;
}

bool VideoEncoder::Seek(int64_t timestamp_ms) {
    if (!is_open_) {
        return false;
    }

    AVStream* stream = fmt_ctx_->streams[video_stream_index_];
    int64_t ts = av_rescale_q(timestamp_ms, {1, 1000}, stream->time_base);
    
    int ret = av_seek_frame(fmt_ctx_, video_stream_index_, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        return false;
    }

    avcodec_flush_buffers(codec_ctx_);
    return true;
}

int64_t VideoEncoder::GetCurrentTimestamp() const {
    if (!is_open_ || video_stream_index_ < 0) {
        return 0;
    }
    
    AVStream* stream = fmt_ctx_->streams[video_stream_index_];
    return av_rescale_q(current_pts_, stream->time_base, {1, 1000});
}

std::string VideoEncoder::GetLastErrorString() const {
    return GetErrorString(last_error_);
}

bool VideoEncoder::InitSwsContext(int src_width, int src_height, int src_format) {
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
    }

    sws_ctx_ = sws_getContext(
        src_width, src_height, (AVPixelFormat)src_format,
        src_width, src_height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    return sws_ctx_ != nullptr;
}

bool VideoEncoder::ConvertFrame(AVFrame* src_frame, VideoFrame& dst_frame) {
    if (!sws_ctx_) {
        if (!InitSwsContext(src_frame->width, src_frame->height, src_frame->format)) {
            return false;
        }

        // 分配 RGB 帧
        rgb_frame_ = av_frame_alloc();
        if (!rgb_frame_) {
            return false;
        }

        rgb_frame_->format = AV_PIX_FMT_RGB24;
        rgb_frame_->width = src_frame->width;
        rgb_frame_->height = src_frame->height;

        int ret = av_frame_get_buffer(rgb_frame_, 0);
        if (ret < 0) {
            return false;
        }
    }

    // 执行转换
    sws_scale(sws_ctx_, src_frame->data, src_frame->linesize,
              0, src_frame->height, rgb_frame_->data, rgb_frame_->linesize);

    dst_frame.width = rgb_frame_->width;
    dst_frame.height = rgb_frame_->height;
    dst_frame.pts = src_frame->pts;
    dst_frame.dts = src_frame->pkt_dts;
    dst_frame.is_key_frame = src_frame->key_frame;
    
    for (int i = 0; i < 4; i++) {
        dst_frame.data[i] = rgb_frame_->data[i];
        dst_frame.linesize[i] = rgb_frame_->linesize[i];
    }

    return true;
}

} // namespace VideoCodec
