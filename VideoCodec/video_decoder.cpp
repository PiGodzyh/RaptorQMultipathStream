/**
 * VideoDecoder 实现
 */

#include "video_decoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <iostream>
#include <cstring>

namespace VideoCodec {

VideoDecoder::VideoDecoder()
    : is_open_(false)
    , last_error_(ErrorCode::SUCCESS)
    , frame_count_(0)
    , fmt_ctx_(nullptr)
    , codec_ctx_(nullptr)
    , av_frame_(nullptr)
    , packet_(nullptr)
    , sws_ctx_(nullptr)
    , video_stream_(nullptr)
    , tmp_frame_(nullptr)
    , tmp_buffer_(nullptr)
    , next_pts_(0) {
}

VideoDecoder::~VideoDecoder() {
    Close();
}

bool VideoDecoder::Create(const std::string& filepath, const EncodeParams& params) {
    if (is_open_) {
        Close();
    }

    // 注册所有格式和编码器
    // FFmpeg 4.0+ 无需 av_register_all()，格式与编解码器会自动注册

    last_error_ = ErrorCode::SUCCESS;
    encode_params_ = params;
    frame_count_ = 0;
    next_pts_ = 0;

    // 分配格式上下文 (尝试使用 MP4 格式)
    const char* format_name = "mp4";
    int ret = avformat_alloc_output_context2(&fmt_ctx_, nullptr, format_name, filepath.c_str());
    if (ret < 0 || !fmt_ctx_) {
        std::cerr << "Cannot allocate output context" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        return false;
    }

    // 查找编码器
    const AVCodec* codec = nullptr;
    if (!params.codec_name.empty()) {
        codec = avcodec_find_encoder_by_name(params.codec_name.c_str());
    }
    if (!codec) {
        // 默认使用 H.264
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }

    if (!codec) {
        std::cerr << "Cannot find encoder" << std::endl;
        last_error_ = ErrorCode::CODEC_NOT_FOUND;
        Close();
        return false;
    }

    // 创建视频流
    video_stream_ = avformat_new_stream(fmt_ctx_, codec);
    if (!video_stream_) {
        std::cerr << "Cannot create video stream" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        Close();
        return false;
    }

    video_stream_->id = fmt_ctx_->nb_streams - 1;

    // 创建编码器上下文
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "Cannot allocate codec context" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        Close();
        return false;
    }

    // 设置编码参数
    codec_ctx_->width = params.width;
    codec_ctx_->height = params.height;
    
    // 设置时间基和帧率
    AVRational time_base = {params.fps_den, params.fps_num};
    codec_ctx_->time_base = time_base;
    video_stream_->time_base = time_base;
    codec_ctx_->framerate = {params.fps_num, params.fps_den};
    
    // 设置像素格式
    AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;
    if (!params.pixel_format.empty()) {
        pix_fmt = av_get_pix_fmt(params.pixel_format.c_str());
        if (pix_fmt == AV_PIX_FMT_NONE) {
            pix_fmt = AV_PIX_FMT_YUV420P;
        }
    }
    codec_ctx_->pix_fmt = pix_fmt;

    // 设置比特率
    if (params.bitrate > 0) {
        codec_ctx_->bit_rate = params.bitrate;
    }

    // GOP 大小
    codec_ctx_->gop_size = 12;
    codec_ctx_->max_b_frames = 2;

    // 设置编码器选项
    if (fmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // 设置 x264 预设（如果可用）
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(codec_ctx_->priv_data, "preset", "medium", 0);
        av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0);
    }

    // 打开编码器
    ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Cannot open codec: " << errbuf << std::endl;
        last_error_ = ErrorCode::ENCODER_OPEN_FAILED;
        Close();
        return false;
    }

    // 复制编码器参数到流
    ret = avcodec_parameters_from_context(video_stream_->codecpar, codec_ctx_);
    if (ret < 0) {
        std::cerr << "Cannot copy codec parameters" << std::endl;
        last_error_ = ErrorCode::ENCODER_OPEN_FAILED;
        Close();
        return false;
    }

    // 打开输出文件
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx_->pb, filepath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Cannot open output file: " << errbuf << std::endl;
            last_error_ = ErrorCode::WRITE_FAILED;
            Close();
            return false;
        }
    }

    // 写入文件头
    ret = avformat_write_header(fmt_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Cannot write header: " << errbuf << std::endl;
        last_error_ = ErrorCode::WRITE_FAILED;
        Close();
        return false;
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

    // 为输入帧分配缓冲区（假设是 YUV420P）
    av_frame_->format = pix_fmt;
    av_frame_->width = params.width;
    av_frame_->height = params.height;

    ret = av_frame_get_buffer(av_frame_, 0);
    if (ret < 0) {
        std::cerr << "Cannot allocate frame buffer" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        Close();
        return false;
    }

    // 初始化临时帧用于格式转换
    if (!InitTmpFrame()) {
        Close();
        return false;
    }

    is_open_ = true;

    std::cout << "VideoDecoder: Created " << filepath << std::endl;
    std::cout << "  Format: " << fmt_ctx_->oformat->name << std::endl;
    std::cout << "  Codec: " << codec->name << std::endl;
    std::cout << "  Resolution: " << params.width << "x" << params.height << std::endl;
    std::cout << "  FPS: " << params.fps_num << "/" << params.fps_den << std::endl;
    std::cout << "  Pixel Format: " << av_get_pix_fmt_name(pix_fmt) << std::endl;

    return true;
}

void VideoDecoder::Close() {
    if (is_open_) {
        Flush();
        
        if (fmt_ctx_) {
            av_write_trailer(fmt_ctx_);
        }
    }

    if (tmp_buffer_) {
        av_free(tmp_buffer_);
        tmp_buffer_ = nullptr;
    }

    if (tmp_frame_) {
        av_frame_free(&tmp_frame_);
        tmp_frame_ = nullptr;
    }

    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
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
        if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE) && fmt_ctx_->pb) {
            avio_closep(&fmt_ctx_->pb);
        }
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
    }

    video_stream_ = nullptr;
    is_open_ = false;
    next_pts_ = 0;
}

bool VideoDecoder::WriteFrame(const VideoFrame& frame) {
    if (!is_open_) {
        return false;
    }

    last_error_ = ErrorCode::SUCCESS;

    // 确保帧可写
    int ret = av_frame_make_writable(av_frame_);
    if (ret < 0) {
        last_error_ = ErrorCode::WRITE_FAILED;
        return false;
    }

    // 复制帧数据
    if (frame.data[0] && frame.linesize[0] > 0) {
        // 假设是 YUV420P 格式
        for (int i = 0; i < 3; i++) {
            if (frame.data[i]) {
                int h = (i == 0) ? av_frame_->height : av_frame_->height / 2;
                for (int y = 0; y < h; y++) {
                    memcpy(av_frame_->data[i] + y * av_frame_->linesize[i],
                           frame.data[i] + y * frame.linesize[i],
                           frame.linesize[i]);
                }
            }
        }
    }

    av_frame_->pts = next_pts_++;

    return EncodeAndWriteFrame(av_frame_);
}

bool VideoDecoder::WriteYUVData(const uint8_t* y_data, const uint8_t* u_data, const uint8_t* v_data,
                                 int64_t pts) {
    if (!is_open_) {
        return false;
    }

    last_error_ = ErrorCode::SUCCESS;

    // 确保帧可写
    int ret = av_frame_make_writable(av_frame_);
    if (ret < 0) {
        last_error_ = ErrorCode::WRITE_FAILED;
        return false;
    }

    // 复制 Y 平面
    for (int y = 0; y < av_frame_->height; y++) {
        memcpy(av_frame_->data[0] + y * av_frame_->linesize[0],
               y_data + y * av_frame_->width,
               av_frame_->width);
    }

    // 复制 U 和 V 平面 (YUV420P，宽高减半)
    int uv_height = av_frame_->height / 2;
    int uv_width = av_frame_->width / 2;
    
    for (int y = 0; y < uv_height; y++) {
        memcpy(av_frame_->data[1] + y * av_frame_->linesize[1],
               u_data + y * uv_width,
               uv_width);
        memcpy(av_frame_->data[2] + y * av_frame_->linesize[2],
               v_data + y * uv_width,
               uv_width);
    }

    av_frame_->pts = (pts >= 0) ? pts : next_pts_++;

    return EncodeAndWriteFrame(av_frame_);
}

bool VideoDecoder::EncodeAndWriteFrame(AVFrame* frame) {
    // 发送帧到编码器
    int ret = avcodec_send_frame(codec_ctx_, frame);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Error sending frame to encoder: " << errbuf << std::endl;
        last_error_ = ErrorCode::WRITE_FAILED;
        return false;
    }

    // 接收编码后的包
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Error encoding frame: " << errbuf << std::endl;
            last_error_ = ErrorCode::WRITE_FAILED;
            return false;
        }

        // 调整时间戳
        av_packet_rescale_ts(packet_, codec_ctx_->time_base, video_stream_->time_base);
        packet_->stream_index = video_stream_->index;

        // 写入文件
        ret = av_interleaved_write_frame(fmt_ctx_, packet_);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Error writing packet: " << errbuf << std::endl;
            last_error_ = ErrorCode::WRITE_FAILED;
            return false;
        }

        av_packet_unref(packet_);
        frame_count_++;
    }

    return true;
}

bool VideoDecoder::Flush() {
    if (!is_open_) {
        return true;
    }

    // 发送空帧刷新编码器
    return EncodeAndWriteFrame(nullptr);
}

VideoFrame VideoDecoder::GetFrameTemplate() const {
    VideoFrame frame;
    frame.width = encode_params_.width;
    frame.height = encode_params_.height;
    
    if (av_frame_) {
        for (int i = 0; i < 4; i++) {
            frame.linesize[i] = av_frame_->linesize[i];
        }
    }
    
    return frame;
}

std::string VideoDecoder::GetLastErrorString() const {
    return GetErrorString(last_error_);
}

bool VideoDecoder::InitTmpFrame() {
    // 分配临时帧用于格式转换
    tmp_frame_ = av_frame_alloc();
    if (!tmp_frame_) {
        return false;
    }

    tmp_frame_->format = codec_ctx_->pix_fmt;
    tmp_frame_->width = codec_ctx_->width;
    tmp_frame_->height = codec_ctx_->height;

    int size = av_image_alloc(tmp_frame_->data, tmp_frame_->linesize,
                              tmp_frame_->width, tmp_frame_->height,
                              codec_ctx_->pix_fmt, 32);
    if (size < 0) {
        av_frame_free(&tmp_frame_);
        tmp_frame_ = nullptr;
        return false;
    }

    tmp_buffer_ = tmp_frame_->data[0]; // 保存指针以便释放
    return true;
}

} // namespace VideoCodec
