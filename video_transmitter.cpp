/**
 * video_transmitter.cpp - 视频发送端实现
 */

#include "video_transmitter.h"
#include "pack/rq_pack.h"
#include "common.h"
#include "log.h"

#include <iostream>
#include <cstring>

namespace VideoTransmit {

VideoTransmitter::VideoTransmitter(std::shared_ptr<UnifiedSender> unified_sender)
    : video_reader_(std::make_unique<VideoCodec::VideoReader>())
    , video_opened_(false)
    , unified_sender_(unified_sender)
    , running_(false)
    , send_thread_running_(false)
    , frame_seq_counter_(0)  // 从0开始，配置包用0，视频帧从1开始
    , gop_counter_(0)
    , block_id_counter_(1)  // 从1开始，0保留给配置
    , current_gop_id_(0)
    , frame_in_gop_(0) {
}

VideoTransmitter::~VideoTransmitter() {
    Stop();
    CloseVideoFile();
}

bool VideoTransmitter::OpenVideoFile(const std::string& filepath) {
    if (running_) {
        std::cerr << "VideoTransmitter: Cannot open file while running" << std::endl;
        return false;
    }
    
    if (video_opened_) {
        CloseVideoFile();
    }
    
    if (!video_reader_->Open(filepath)) {
        std::cerr << "VideoTransmitter: Failed to open video file: " << filepath << std::endl;
        return false;
    }
    
    video_opened_ = true;
    current_gop_id_ = 0;
    frame_in_gop_ = 0;
    
    std::cout << "VideoTransmitter: Opened " << filepath << std::endl;
    return true;
}

void VideoTransmitter::CloseVideoFile() {
    if (video_opened_) {
        video_reader_->Close();
        video_opened_ = false;
    }
}

void VideoTransmitter::Start() {
    if (running_) {
        return;
    }
    
    if (!video_opened_) {
        ReportError("Video file not opened");
        return;
    }
    
    if (!unified_sender_) {
        ReportError("UnifiedSender not set");
        return;
    }
    
    running_ = true;
    send_thread_running_ = true;
    
    // 启动发送线程
    send_thread_ = std::thread(&VideoTransmitter::SendThreadFunc, this);
    
    std::cout << "VideoTransmitter: Started (using UnifiedSender)" << std::endl;
}

void VideoTransmitter::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    send_thread_running_ = false;
    
    // 等待发送线程结束
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
    
    // 注意：UnifiedSender 由外部管理，不在此停止
    
    std::cout << "VideoTransmitter: Stopped" << std::endl;
}

void VideoTransmitter::SetSendCallback(VideoSendCallback callback) {
    send_callback_ = callback;
}

void VideoTransmitter::SetErrorCallback(VideoErrorCallback callback) {
    error_callback_ = callback;
}

bool VideoTransmitter::IsVideoOpen() const {
    return video_opened_;
}

VideoTransmitStats VideoTransmitter::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

VideoCodec::VideoInfo VideoTransmitter::GetVideoInfo() const {
    if (video_opened_) {
        return video_reader_->GetVideoInfo();
    }
    return VideoCodec::VideoInfo();
}

void VideoTransmitter::SendThreadFunc() {
    std::cout << "VideoTransmitter: Send thread started" << std::endl;
    
    // 首先发送视频配置
    if (!SendVideoConfig()) {
        ReportError("Failed to send video config");
        return;
    }
    
    // 获取视频帧率，计算帧间隔
    auto info = video_reader_->GetVideoInfo();
    int fps_interval_us = 0;
    if (info.fps_num > 0 && info.fps_den > 0) {
        fps_interval_us = (info.fps_den * 1000000) / info.fps_num;
        std::cout << "VideoTransmitter: Video FPS=" << info.fps_num << "/" << info.fps_den
                  << ", frame interval=" << fps_interval_us << "us" << std::endl;
    } else {
        // 默认 25fps
        fps_interval_us = 40000;
        std::cout << "VideoTransmitter: Unknown FPS, defaulting to 25fps (40000us)" << std::endl;
    }
    
    // 读取并发送帧
    VideoCodec::EncodedFrame frame;
    int frame_count = 0;
    auto next_frame_time = std::chrono::steady_clock::now();
    
    while (send_thread_running_ && video_reader_->ReadFrame(frame)) {
        frame_count++;
        
        if (!SendFrame(frame)) {
            ReportError("Failed to send frame " + std::to_string(frame_count));
            continue;
        }
        
        // 按帧率间隔发送：sleep_until 保证即使发送耗时波动也能对齐时间轴
        next_frame_time += std::chrono::microseconds(fps_interval_us);
        std::this_thread::sleep_until(next_frame_time);
        
        // GOP管理
        if (frame.is_key_frame) {
            current_gop_id_++;
            frame_in_gop_ = 0;
        }
        frame_in_gop_++;
    }
    
    std::cout << "VideoTransmitter: Send thread finished, sent " << frame_count << " frames" << std::endl;
}

bool VideoTransmitter::SendVideoConfig() {
    auto info = video_reader_->GetVideoInfo();
    
    // 构建配置包
    VideoConfigPacket config_packet;
    config_packet.config.width = info.width;
    config_packet.config.height = info.height;
    config_packet.config.fps_num = info.fps_num;
    config_packet.config.fps_den = info.fps_den;
    config_packet.config.bitrate = info.bitrate;
    config_packet.config.gop_size = info.gop_size;
    strncpy(config_packet.config.codec_name, info.codec_name.c_str(), 
            sizeof(config_packet.config.codec_name) - 1);
    config_packet.config.extradata_size = info.extradata.size();
    config_packet.extradata = info.extradata;
    
    // 序列化配置数据
    auto config_data = config_packet.Serialize();
    
    // 构建带头部的数据
    VideoFrameHeader header;
    header.frame_type = FrameType::CONFIG;
    header.flags = FLAG_CONFIG_FRAME;
    header.frame_size = config_data.size();
    header.frame_seq = frame_seq_counter_++;
    
    std::vector<uint8_t> packet_data(sizeof(VideoFrameHeader) + config_data.size());
    memcpy(packet_data.data(), &header, sizeof(VideoFrameHeader));
    memcpy(packet_data.data() + sizeof(VideoFrameHeader), config_data.data(), config_data.size());
    
    // 使用 UnifiedSender 发送（内部会自动进行 RaptorQ 编码）
    // stream_id = 0 保留给配置包
    unified_sender_->send(DataPriority::VIDEO, 0, packet_data);
    
    std::cout << "VideoTransmitter: Sent video config (" << packet_data.size() << " bytes)" << std::endl;
    
    // 等待配置发送完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    return true;
}

bool VideoTransmitter::SendFrame(const VideoCodec::EncodedFrame& frame) {
    uint32_t frame_seq = frame_seq_counter_++;
    
    // 确定帧类型
    FrameType frame_type;
    switch (frame.type) {
        case VideoCodec::FrameType::I_FRAME:
            frame_type = FrameType::I_FRAME;
            break;
        case VideoCodec::FrameType::P_FRAME:
            frame_type = FrameType::P_FRAME;
            break;
        case VideoCodec::FrameType::B_FRAME:
            frame_type = FrameType::B_FRAME;
            break;
        default:
            frame_type = frame.is_key_frame ? FrameType::I_FRAME : FrameType::P_FRAME;
    }
    
    // 构建帧数据
    auto frame_data = BuildFrameData(frame, frame_seq);
    
    // Source Block ID（从1开始，0保留给配置）
    uint32_t block_id = block_id_counter_++;
    
    // 使用 UnifiedSender 发送（内部会自动进行 RaptorQ 编码和调度）
    unified_sender_->send(DataPriority::VIDEO, block_id, frame_data);
    
    // 更新统计（符号数估算）
    auto params = GetTransmitParams(frame_type);
    uint32_t est_source_symbols = (frame_data.size() + params.symbol_size - 1) / params.symbol_size;
    uint32_t est_total_symbols = static_cast<uint32_t>(est_source_symbols * (1 + params.repair_ratio));
    UpdateStats(frame_type, frame_data.size(), est_total_symbols);
    
    // 回调
    if (send_callback_) {
        send_callback_(frame_seq, frame_type, frame_data.size(), true);
    }
    
    // 打印进度（DEBUG级别，每300帧打印一次，避免淹没终端）
    if (frame_seq % 300 == 0) {
        LOG_MODULE_DEBUG(DataPriority::VIDEO, "Sent frame " << frame_seq 
                  << " (" << VideoCodec::FrameTypeToString(static_cast<VideoCodec::FrameType>(frame.type))
                  << ", " << frame.data.size() << " bytes)");
    }
    
    return true;
}

std::vector<uint8_t> VideoTransmitter::BuildFrameData(const VideoCodec::EncodedFrame& frame,
                                                       uint32_t frame_seq) {
    // 构建视频帧头部
    VideoFrameHeader header;
    
    // 显式转换帧类型（两个枚举定义不同，不能static_cast）
    switch (frame.type) {
        case VideoCodec::FrameType::I_FRAME:
            header.frame_type = FrameType::I_FRAME;
            break;
        case VideoCodec::FrameType::P_FRAME:
            header.frame_type = FrameType::P_FRAME;
            break;
        case VideoCodec::FrameType::B_FRAME:
            header.frame_type = FrameType::B_FRAME;
            break;
        default:
            header.frame_type = frame.is_key_frame ? FrameType::I_FRAME : FrameType::P_FRAME;
            break;
    }
    header.flags = FLAG_FIRST_SYMBOL | FLAG_LAST_SYMBOL;
    header.pts = frame.pts;
    header.dts = frame.dts;
    header.gop_id = current_gop_id_;
    header.frame_in_gop = frame_in_gop_;
    header.frame_size = frame.data.size();
    header.frame_seq = frame_seq;
    header.source_block_id = block_id_counter_.load();
    
    // 计算总符号数
    auto params = GetTransmitParams(header.frame_type);
    uint32_t k = (frame.data.size() + params.symbol_size - 1) / params.symbol_size;
    uint32_t n = static_cast<uint32_t>(k * (1 + params.repair_ratio));
    header.total_symbols = n;
    
    // 组装数据：头部 + H.264数据
    std::vector<uint8_t> data(sizeof(VideoFrameHeader) + frame.data.size());
    memcpy(data.data(), &header, sizeof(VideoFrameHeader));
    memcpy(data.data() + sizeof(VideoFrameHeader), frame.data.data(), frame.data.size());
    
    return data;
}

void VideoTransmitter::UpdateStats(FrameType type, size_t bytes_sent, uint32_t symbols) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.frames_sent++;
    stats_.bytes_sent += bytes_sent;
    stats_.symbols_sent += symbols;
    stats_.source_blocks_sent++;
    
    switch (type) {
        case FrameType::I_FRAME:
            stats_.i_frames_sent++;
            break;
        case FrameType::P_FRAME:
            stats_.p_frames_sent++;
            break;
        case FrameType::B_FRAME:
            stats_.b_frames_sent++;
            break;
        default:
            break;
    }
}

void VideoTransmitter::ReportError(const std::string& error) {
    std::cerr << "VideoTransmitter Error: " << error << std::endl;
    if (error_callback_) {
        error_callback_(error);
    }
}

} // namespace VideoTransmit
