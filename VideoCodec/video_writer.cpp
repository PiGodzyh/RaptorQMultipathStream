/**
 * video_writer.cpp - 视频写入器实现 (H.264 NAL透传)
 * 
 * 将H.264编码数据（NAL单元）直接写入MP4容器，不解码/编码
 */

#include "video_writer.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
}

#include <iostream>
#include <cstring>

namespace VideoCodec {

VideoWriter::VideoWriter()
    : is_open_(false)
    , last_error_(ErrorCode::SUCCESS)
    , frame_count_(0)
    , fmt_ctx_(nullptr)
    , video_stream_(nullptr)
    , packet_(nullptr) {
}

VideoWriter::~VideoWriter() {
    Close();
}

VideoWriter::VideoWriter(VideoWriter&& other) noexcept
    : is_open_(other.is_open_)
    , last_error_(other.last_error_)
    , video_params_(other.video_params_)
    , frame_count_(other.frame_count_)
    , fmt_ctx_(other.fmt_ctx_)
    , video_stream_(other.video_stream_)
    , packet_(other.packet_) {
    other.is_open_ = false;
    other.fmt_ctx_ = nullptr;
    other.video_stream_ = nullptr;
    other.packet_ = nullptr;
    other.frame_count_ = 0;
}

VideoWriter& VideoWriter::operator=(VideoWriter&& other) noexcept {
    if (this != &other) {
        Close();
        is_open_ = other.is_open_;
        last_error_ = other.last_error_;
        video_params_ = other.video_params_;
        frame_count_ = other.frame_count_;
        fmt_ctx_ = other.fmt_ctx_;
        video_stream_ = other.video_stream_;
        packet_ = other.packet_;
        
        other.is_open_ = false;
        other.fmt_ctx_ = nullptr;
        other.video_stream_ = nullptr;
        other.packet_ = nullptr;
        other.frame_count_ = 0;
    }
    return *this;
}

bool VideoWriter::Create(const std::string& filepath, const VideoParams& params) {
    if (is_open_) {
        Close();
    }

    last_error_ = ErrorCode::SUCCESS;
    video_params_ = params;
    frame_count_ = 0;

    if (!InitOutput(filepath)) {
        return false;
    }

    // 分配packet
    packet_ = av_packet_alloc();
    if (!packet_) {
        std::cerr << "Cannot allocate packet" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        Close();
        return false;
    }

    is_open_ = true;

    std::cout << "VideoWriter: Created " << filepath << std::endl;
    std::cout << "  Format: " << fmt_ctx_->oformat->name << std::endl;
    std::cout << "  Resolution: " << video_params_.width << "x" << video_params_.height << std::endl;
    std::cout << "  FPS: " << video_params_.fps_num << "/" << video_params_.fps_den << std::endl;
    std::cout << "  GOP Size: " << video_params_.gop_size << std::endl;

    return true;
}

bool VideoWriter::InitOutput(const std::string& filepath) {
    // 分配输出上下文
    int ret = avformat_alloc_output_context2(&fmt_ctx_, nullptr, nullptr, filepath.c_str());
    if (ret < 0 || !fmt_ctx_) {
        std::cerr << "Cannot allocate output context" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        return false;
    }

    // 创建视频流
    video_stream_ = avformat_new_stream(fmt_ctx_, nullptr);
    if (!video_stream_) {
        std::cerr << "Cannot create video stream" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        return false;
    }
    video_stream_->id = fmt_ctx_->nb_streams - 1;

    // 设置流的time_base
    video_stream_->time_base = {video_params_.fps_den, video_params_.fps_num};

    // 设置codecpar为H.264
    video_stream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video_stream_->codecpar->codec_id = AV_CODEC_ID_H264;
    video_stream_->codecpar->width = video_params_.width;
    video_stream_->codecpar->height = video_params_.height;
    video_stream_->codecpar->format = AV_PIX_FMT_YUV420P;
    
    if (video_params_.bitrate > 0) {
        video_stream_->codecpar->bit_rate = video_params_.bitrate;
    }
    
    // 复制extradata (SPS/PPS)
    if (!video_params_.extradata.empty()) {
        video_stream_->codecpar->extradata_size = video_params_.extradata.size();
        video_stream_->codecpar->extradata = (uint8_t*)av_malloc(video_params_.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
        if (video_stream_->codecpar->extradata) {
            memcpy(video_stream_->codecpar->extradata, video_params_.extradata.data(), video_params_.extradata.size());
            memset(video_stream_->codecpar->extradata + video_params_.extradata.size(), 0, AV_INPUT_BUFFER_PADDING_SIZE);
        }
    }

    // 打开输出文件
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx_->pb, filepath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Cannot open output file: " << errbuf << std::endl;
            last_error_ = ErrorCode::WRITE_FAILED;
            return false;
        }
    }

    // 写入文件头
    ret = avformat_write_header(fmt_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Cannot write header: " << errbuf << std::endl;
        last_error_ = ErrorCode::HEADER_WRITE_FAILED;
        return false;
    }

    return true;
}

void VideoWriter::Close() {
    if (is_open_) {
        Flush();
        
        if (fmt_ctx_) {
            av_write_trailer(fmt_ctx_);
        }
    }

    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
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
    // 注意：不重置frame_count_，以便Close()后仍能获取统计信息
}

bool VideoWriter::WriteFrame(const EncodedFrame& frame) {
    return WriteH264Data(frame.data.data(), frame.data.size(), 
                         frame.pts, frame.is_key_frame);
}

bool VideoWriter::WriteH264Data(const uint8_t* data, size_t size, 
                                 int64_t pts_ms, bool is_key_frame) {
    if (!is_open_) {
        return false;
    }

    last_error_ = ErrorCode::SUCCESS;

    // 确保数据有效
    if (!data || size == 0) {
        std::cerr << "Invalid H.264 data" << std::endl;
        last_error_ = ErrorCode::WRITE_FAILED;
        return false;
    }

    return WritePacket(data, size, pts_ms, is_key_frame);
}

bool VideoWriter::WritePacket(const uint8_t* data, size_t size,
                               int64_t pts_ms, bool is_key_frame) {
    // 分配packet数据
    int ret = av_new_packet(packet_, size);
    if (ret < 0) {
        std::cerr << "Cannot allocate packet data" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        return false;
    }

    // 复制数据（从MP4读取的已经是AVCC格式，直接写入）
    memcpy(packet_->data, data, size);
    
    // 转换毫秒到流的time_base
    int64_t pts = av_rescale_q(pts_ms, {1, 1000}, video_stream_->time_base);
    
    // 使用单调递增的DTS（MP4 muxer要求DTS单调递增）
    int64_t dts = frame_count_;
    
    // 设置packet属性
    packet_->pts = pts;
    packet_->dts = dts;
    packet_->stream_index = video_stream_->index;
    packet_->duration = av_rescale_q(1000 / video_params_.fps_num, {1, 1000}, video_stream_->time_base);
    
    if (is_key_frame) {
        packet_->flags |= AV_PKT_FLAG_KEY;
    } else {
        packet_->flags = 0;
    }

    // 写入packet
    ret = av_interleaved_write_frame(fmt_ctx_, packet_);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Error writing packet: " << errbuf << std::endl;
        last_error_ = ErrorCode::WRITE_FAILED;
        av_packet_unref(packet_);
        return false;
    }

    frame_count_++;

    return true;
}

bool VideoWriter::Flush() {
    if (!is_open_) {
        return true;
    }

    // 对于H.264透传，不需要特别刷新
    return true;
}

std::string VideoWriter::GetLastErrorString() const {
    return GetErrorString(last_error_);
}

} // namespace VideoCodec
