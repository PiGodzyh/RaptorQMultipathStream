/**
 * video_reader.cpp - 视频读取器实现
 */

#include "video_reader.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include <iostream>
#include <cstring>

namespace VideoCodec {

// H.264 NAL单元类型
enum H264NALType {
    H264_NAL_SLICE = 1,           // 非IDR slice (P/B帧)
    H264_NAL_IDR = 5,             // IDR slice (I帧)
    H264_NAL_SEI = 6,             // SEI
    H264_NAL_SPS = 7,             // SPS
    H264_NAL_PPS = 8,             // PPS
    H264_NAL_AUD = 9,             // AUD
};

const char* GetErrorString(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS: return "Success";
        case ErrorCode::FILE_NOT_FOUND: return "File not found";
        case ErrorCode::INVALID_FORMAT: return "Invalid format";
        case ErrorCode::CODEC_NOT_FOUND: return "Codec not found";
        case ErrorCode::STREAM_NOT_FOUND: return "Video stream not found";
        case ErrorCode::ALLOC_FAILED: return "Memory allocation failed";
        case ErrorCode::READ_FAILED: return "Read failed";
        case ErrorCode::WRITE_FAILED: return "Write failed";
        case ErrorCode::HEADER_WRITE_FAILED: return "Header write failed";
        case ErrorCode::UNKNOWN_ERROR: return "Unknown error";
        default: return "Invalid error code";
    }
}

const char* FrameTypeToString(FrameType type) {
    switch (type) {
        case FrameType::I_FRAME: return "I";
        case FrameType::P_FRAME: return "P";
        case FrameType::B_FRAME: return "B";
        default: return "?";
    }
}

VideoReader::VideoReader()
    : is_open_(false)
    , last_error_(ErrorCode::SUCCESS)
    , fmt_ctx_(nullptr)
    , codec_ctx_(nullptr)
    , packet_(nullptr)
    , video_stream_(nullptr)
    , video_stream_index_(-1)
    , current_pts_(0)
    , current_gop_id_(0)
    , frame_in_gop_(0) {
}

VideoReader::~VideoReader() {
    Close();
}

VideoReader::VideoReader(VideoReader&& other) noexcept
    : is_open_(other.is_open_)
    , last_error_(other.last_error_)
    , video_info_(other.video_info_)
    , fmt_ctx_(other.fmt_ctx_)
    , codec_ctx_(other.codec_ctx_)
    , packet_(other.packet_)
    , video_stream_(other.video_stream_)
    , video_stream_index_(other.video_stream_index_)
    , current_pts_(other.current_pts_)
    , current_gop_id_(other.current_gop_id_)
    , frame_in_gop_(other.frame_in_gop_) {
    other.is_open_ = false;
    other.fmt_ctx_ = nullptr;
    other.codec_ctx_ = nullptr;
    other.packet_ = nullptr;
    other.video_stream_ = nullptr;
}

VideoReader& VideoReader::operator=(VideoReader&& other) noexcept {
    if (this != &other) {
        Close();
        is_open_ = other.is_open_;
        last_error_ = other.last_error_;
        video_info_ = other.video_info_;
        fmt_ctx_ = other.fmt_ctx_;
        codec_ctx_ = other.codec_ctx_;
        packet_ = other.packet_;
        video_stream_ = other.video_stream_;
        video_stream_index_ = other.video_stream_index_;
        current_pts_ = other.current_pts_;
        current_gop_id_ = other.current_gop_id_;
        frame_in_gop_ = other.frame_in_gop_;
        
        other.is_open_ = false;
        other.fmt_ctx_ = nullptr;
        other.codec_ctx_ = nullptr;
        other.packet_ = nullptr;
        other.video_stream_ = nullptr;
    }
    return *this;
}

bool VideoReader::Open(const std::string& filepath) {
    if (is_open_) {
        Close();
    }

    last_error_ = ErrorCode::SUCCESS;

    // 打开输入文件
    int ret = avformat_open_input(&fmt_ctx_, filepath.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Cannot open input file: " << filepath << " - " << errbuf << std::endl;
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
    if (!InitStreams()) {
        Close();
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
    current_pts_ = 0;
    current_gop_id_ = 0;
    frame_in_gop_ = 0;

    std::cout << "VideoReader: Opened " << filepath << std::endl;
    std::cout << "  Format: " << video_info_.format_name << std::endl;
    std::cout << "  Codec: " << video_info_.codec_name << std::endl;
    std::cout << "  Resolution: " << video_info_.width << "x" << video_info_.height << std::endl;
    std::cout << "  FPS: " << video_info_.fps_num << "/" << video_info_.fps_den << std::endl;
    std::cout << "  Duration: " << video_info_.duration_ms << " ms" << std::endl;
    std::cout << "  GOP Size: " << video_info_.gop_size << std::endl;

    return true;
}

bool VideoReader::InitStreams() {
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
        last_error_ = ErrorCode::STREAM_NOT_FOUND;
        return false;
    }

    video_stream_ = fmt_ctx_->streams[video_stream_index_];
    AVCodecParameters* codecpar = video_stream_->codecpar;

    // 查找解码器（仅用于获取信息，不解码）
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "Codec not found" << std::endl;
        last_error_ = ErrorCode::CODEC_NOT_FOUND;
        return false;
    }

    // 创建解码器上下文（仅用于获取信息）
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "Cannot allocate codec context" << std::endl;
        last_error_ = ErrorCode::ALLOC_FAILED;
        return false;
    }

    int ret = avcodec_parameters_to_context(codec_ctx_, codecpar);
    if (ret < 0) {
        std::cerr << "Cannot copy codec parameters" << std::endl;
        last_error_ = ErrorCode::INVALID_FORMAT;
        return false;
    }

    // 填充视频信息
    video_info_.format_name = fmt_ctx_->iformat->name;
    video_info_.codec_name = codec->name;
    video_info_.width = codec_ctx_->width;
    video_info_.height = codec_ctx_->height;
    
    // 计算帧率
    AVRational frame_rate = av_guess_frame_rate(fmt_ctx_, video_stream_, nullptr);
    video_info_.fps_num = frame_rate.num;
    video_info_.fps_den = frame_rate.den;
    
    // 获取时长
    if (video_stream_->duration != AV_NOPTS_VALUE) {
        video_info_.duration_ms = av_rescale_q(video_stream_->duration, 
                                                video_stream_->time_base, {1, 1000});
    } else if (fmt_ctx_->duration != AV_NOPTS_VALUE) {
        video_info_.duration_ms = fmt_ctx_->duration / 1000;
    }
    
    video_info_.bitrate = codecpar->bit_rate;
    video_info_.frame_count = video_stream_->nb_frames;
    video_info_.gop_size = codec_ctx_->gop_size;
    
    // 复制extradata (SPS/PPS)
    if (codecpar->extradata && codecpar->extradata_size > 0) {
        video_info_.extradata.resize(codecpar->extradata_size);
        memcpy(video_info_.extradata.data(), codecpar->extradata, codecpar->extradata_size);
    }

    return true;
}

void VideoReader::Close() {
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
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
    video_stream_ = nullptr;
    current_pts_ = 0;
}

bool VideoReader::ReadFrame(EncodedFrame& frame) {
    if (!is_open_) {
        return false;
    }

    last_error_ = ErrorCode::SUCCESS;

    while (true) {
        int ret = av_read_frame(fmt_ctx_, packet_);
        
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                return false;
            }
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Error reading frame: " << errbuf << std::endl;
            last_error_ = ErrorCode::READ_FAILED;
            return false;
        }

        // 只处理视频流
        if (packet_->stream_index != video_stream_index_) {
            av_packet_unref(packet_);
            continue;
        }

        // 复制数据到frame
        frame.data.resize(packet_->size);
        memcpy(frame.data.data(), packet_->data, packet_->size);
        
        // 设置时间戳
        frame.pts = ConvertPtsToMs(packet_->pts);
        frame.dts = ConvertPtsToMs(packet_->dts);
        
        // 判断是否关键帧
        frame.is_key_frame = (packet_->flags & AV_PKT_FLAG_KEY) != 0;
        
        // 检测帧类型
        frame.type = DetectFrameType(frame.data.data(), frame.data.size(), frame.is_key_frame);
        
        // GOP管理
        if (frame.is_key_frame || frame.type == FrameType::I_FRAME) {
            current_gop_id_++;
            frame_in_gop_ = 0;
        }
        frame.gop_id = current_gop_id_;
        frame_in_gop_++;

        current_pts_ = packet_->pts;
        
        av_packet_unref(packet_);
        return true;
    }
}

FrameType VideoReader::DetectFrameType(const uint8_t* data, size_t size, bool is_key_frame) {
    if (size < 5) {
        return FrameType::UNKNOWN;
    }

    // 跳过起始码 00 00 00 01 或 00 00 01
    size_t offset = 0;
    if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) {
        offset = 4;
    } else if (size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1) {
        offset = 3;
    }

    if (offset == 0 || offset >= size) {
        return FrameType::UNKNOWN;
    }

    // 获取NAL单元类型
    uint8_t nal_ref_idc = (data[offset] >> 5) & 0x3;
    uint8_t nal_unit_type = data[offset] & 0x1F;

    // 判断帧类型
    if (nal_unit_type == H264_NAL_IDR || is_key_frame) {
        return FrameType::I_FRAME;
    } else if (nal_unit_type == H264_NAL_SLICE) {
        // 通过参考级别判断P或B帧
        // 简单判断：如果nal_ref_idc > 0 认为是P帧，否则可能是B帧
        // 更准确的判断需要解析slice header
        return (nal_ref_idc > 0) ? FrameType::P_FRAME : FrameType::B_FRAME;
    }

    return FrameType::UNKNOWN;
}

int64_t VideoReader::ConvertPtsToMs(int64_t pts) const {
    if (pts == AV_NOPTS_VALUE || !video_stream_) {
        return 0;
    }
    return av_rescale_q(pts, video_stream_->time_base, {1, 1000});
}

int VideoReader::ReadAllFrames(FrameReadCallback callback) {
    if (!is_open_ || !callback) {
        return 0;
    }

    int count = 0;
    EncodedFrame frame;
    
    while (ReadFrame(frame)) {
        count++;
        if (!callback(frame)) {
            break;
        }
    }
    
    return count;
}

bool VideoReader::Seek(int64_t timestamp_ms) {
    if (!is_open_ || !video_stream_) {
        return false;
    }

    int64_t ts = av_rescale_q(timestamp_ms, {1, 1000}, video_stream_->time_base);
    
    int ret = av_seek_frame(fmt_ctx_, video_stream_index_, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        return false;
    }

    return true;
}

int64_t VideoReader::GetCurrentTimestamp() const {
    return ConvertPtsToMs(current_pts_);
}

std::string VideoReader::GetLastErrorString() const {
    return GetErrorString(last_error_);
}

} // namespace VideoCodec
