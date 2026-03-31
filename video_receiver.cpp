/**
 * video_receiver.cpp - 视频接收端实现
 */

#include "video_receiver.h"
#include "common.h"

#include <iostream>
#include <cstring>
#include <thread>

namespace VideoTransmit {

VideoReceiver::VideoReceiver(uint16_t port, uint32_t thread_count)
    : receiver_(std::make_unique<Receiver>(this, port, thread_count))
    , port_(port)
    , video_writer_(std::make_unique<VideoCodec::VideoWriter>())
    , output_opened_(false)
    , config_received_(false)
    , running_(false)
    , next_expected_frame_seq_(0) {
}

VideoReceiver::~VideoReceiver() {
    Stop();
    CloseOutputFile();
}

void VideoReceiver::Start() {
    if (running_) {
        return;
    }
    
    running_ = true;
    
    // 启动接收器（在单独线程中运行，因为start()是阻塞的）
    std::thread receiver_thread([this]() {
        receiver_->start();
    });
    receiver_thread.detach();
    
    std::cout << "VideoReceiver: Started on port " << port_ << std::endl;
}

void VideoReceiver::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // 停止接收器
    receiver_->stop();
    
    std::cout << "VideoReceiver: Stopped" << std::endl;
}

bool VideoReceiver::CreateOutputFile(const std::string& filepath) {
    if (output_opened_) {
        CloseOutputFile();
    }
    
    // 等待视频配置
    std::cout << "Waiting for video config..." << std::endl;
    int retry = 0;
    while (!config_received_ && retry < 300) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retry++;
        if (retry % 10 == 0) {
            std::cout << "  waiting... (" << retry * 100 << "ms)" << std::endl;
        }
    }
    
    if (!config_received_) {
        ReportError("Video config not received, cannot create output file");
        return false;
    }
    
    // 构建视频参数
    VideoCodec::VideoWriterParams params;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        params.width = video_config_.width;
        params.height = video_config_.height;
        params.fps_num = video_config_.fps_num;
        params.fps_den = video_config_.fps_den;
        params.bitrate = video_config_.bitrate;
        params.gop_size = video_config_.gop_size;
        params.codec_name = video_config_.codec_name;
        params.extradata = extradata_;
    }
    
    if (!video_writer_->Create(filepath, params)) {
        ReportError("Failed to create output file: " + filepath);
        return false;
    }
    
    output_opened_ = true;
    std::cout << "VideoReceiver: Created output file " << filepath << std::endl;
    return true;
}

void VideoReceiver::CloseOutputFile() {
    if (output_opened_) {
        video_writer_->Close();
        output_opened_ = false;
    }
}

void VideoReceiver::SetFrameCallback(VideoFrameCallback callback) {
    frame_callback_ = callback;
}

void VideoReceiver::SetConfigCallback(VideoConfigCallback callback) {
    config_callback_ = callback;
}

void VideoReceiver::SetErrorCallback(VideoErrorCallback callback) {
    error_callback_ = callback;
}

bool VideoReceiver::IsOutputOpen() const {
    return output_opened_;
}

VideoTransmitStats VideoReceiver::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

bool VideoReceiver::GetVideoConfig(VideoConfig& config) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (!config_received_) {
        return false;
    }
    config = video_config_;
    return true;
}

void VideoReceiver::OnDecodeComplete(uint32_t stream_id, const std::vector<uint8_t>& data) {
    std::cout << "[VideoReceiver] OnDecodeComplete: stream_id=" << stream_id 
              << ", data_size=" << data.size() << std::endl;
    
    if (data.size() < sizeof(VideoFrameHeader)) {
        ReportError("Received data too small: " + std::to_string(data.size()));
        return;
    }
    
    // 解析头部
    VideoFrameHeader header;
    memcpy(&header, data.data(), sizeof(VideoFrameHeader));
    
    std::cout << "[VideoReceiver] Frame type=" << static_cast<int>(header.frame_type)
              << ", seq=" << header.frame_seq << std::endl;
    
    // 根据类型处理
    if (header.frame_type == FrameType::CONFIG) {
        ProcessVideoConfig(data);
    } else {
        ProcessVideoFrame(data);
    }
}

void VideoReceiver::ProcessVideoConfig(const std::vector<uint8_t>& data) {
    if (data.size() <= sizeof(VideoFrameHeader)) {
        ReportError("Invalid config data size");
        return;
    }
    
    // 提取配置数据
    size_t config_size = data.size() - sizeof(VideoFrameHeader);
    const uint8_t* config_data = data.data() + sizeof(VideoFrameHeader);
    
    // 反序列化配置
    VideoConfigPacket config_packet;
    if (!config_packet.Deserialize(config_data, config_size)) {
        ReportError("Failed to deserialize video config");
        return;
    }
    
    // 保存配置
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        video_config_ = config_packet.config;
        extradata_ = config_packet.extradata;
        config_received_ = true;
    }
    
    // 配置接收后，下一个期望的视频帧是seq=1
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (next_expected_frame_seq_ == 0) {
            next_expected_frame_seq_ = 1;
            std::cout << "VideoReceiver: Config received, expecting video frame seq=1" << std::endl;
        }
    }
    
    std::cout << "VideoReceiver: Received video config" << std::endl;
    std::cout << "  Resolution: " << video_config_.width << "x" << video_config_.height << std::endl;
    std::cout << "  FPS: " << video_config_.fps_num << "/" << video_config_.fps_den << std::endl;
    std::cout << "  GOP: " << video_config_.gop_size << std::endl;
    
    // 回调
    if (config_callback_) {
        config_callback_(video_config_);
    }
}

void VideoReceiver::ProcessVideoFrame(const std::vector<uint8_t>& data) {
    std::cout << "[VideoReceiver] ProcessVideoFrame: " << data.size() << " bytes" << std::endl;
    
    if (data.size() <= sizeof(VideoFrameHeader)) {
        ReportError("Invalid frame data size: " + std::to_string(data.size()));
        return;
    }
    
    // 解析头部
    VideoFrameHeader header;
    memcpy(&header, data.data(), sizeof(VideoFrameHeader));
    
    // 提取H.264数据
    size_t frame_data_size = data.size() - sizeof(VideoFrameHeader);
    const uint8_t* frame_data = data.data() + sizeof(VideoFrameHeader);
    
    // 构建EncodedFrame
    VideoCodec::EncodedFrame frame;
    frame.data.resize(header.frame_size);
    memcpy(frame.data.data(), frame_data, std::min(frame.data.size(), frame_data_size));
    frame.pts = header.pts;
    frame.dts = header.dts;
    frame.is_key_frame = (header.frame_type == FrameType::I_FRAME);
    frame.type = static_cast<VideoCodec::FrameType>(header.frame_type);
    frame.gop_id = header.gop_id;
    
    // 保序处理
    bool should_process = false;
    uint32_t written_count = 0;
    uint32_t dropped_full_count = 0;
    uint32_t dropped_old_count = 0;
    
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        
        // 如果是第一个视频帧（next_expected_frame_seq_ 为 0），直接接受它作为起始
        if (next_expected_frame_seq_ == 0) {
            next_expected_frame_seq_ = header.frame_seq;
            std::cout << "VideoReceiver: First frame received, starting from seq=" 
                      << header.frame_seq << std::endl;
        }
        
        if (header.frame_seq == next_expected_frame_seq_) {
            // 期望的帧，直接处理
            should_process = true;
            next_expected_frame_seq_++;
            
            // 处理缓存的帧
            while (!pending_frames_.empty()) {
                auto it = pending_frames_.find(next_expected_frame_seq_);
                if (it == pending_frames_.end()) {
                    break;
                }
                
                // 写入缓存帧
                if (output_opened_) {
                    video_writer_->WriteFrame(it->second);
                }
                if (frame_callback_) {
                    frame_callback_(it->second);
                }
                
                pending_frames_.erase(it);
                next_expected_frame_seq_++;
                written_count++;
            }
        } else if (header.frame_seq > next_expected_frame_seq_) {
            // 乱序到达，放入缓存
            pending_frames_[header.frame_seq] = frame;
            
            // 缓存过大时丢弃最旧的帧（避免无限等待）
            if (pending_frames_.size() > 100) {
                // 丢弃最小的（最旧的）帧
                auto it = pending_frames_.begin();
                uint32_t dropped_seq = it->first;
                pending_frames_.erase(it);
                frame_stats_.frames_dropped_full++;
                
                // 更新期望序号为新的最小值（如果缓存不为空）
                if (!pending_frames_.empty()) {
                    next_expected_frame_seq_ = pending_frames_.begin()->first;
                    std::cout << "\n>>> [帧丢弃] 缓存溢出，丢弃帧 seq=" << dropped_seq 
                              << "，跳至期望=" << next_expected_frame_seq_ << "\n";
                    
                    // 重要：检查新期望的帧是否已经在缓存中
                    // 连续处理缓存中所有连续的帧
                    while (!pending_frames_.empty()) {
                        auto next_it = pending_frames_.find(next_expected_frame_seq_);
                        if (next_it == pending_frames_.end()) {
                            break; // 期望的帧还没来
                        }
                        
                        // 写入缓存帧
                        if (output_opened_) {
                            video_writer_->WriteFrame(next_it->second);
                        }
                        if (frame_callback_) {
                            frame_callback_(next_it->second);
                        }
                        
                        pending_frames_.erase(next_it);
                        next_expected_frame_seq_++;
                        written_count++;
                        frame_stats_.frames_written++;
                    }
                } else {
                    std::cout << "\n>>> [帧丢弃] 缓存溢出，丢弃帧 seq=" << dropped_seq 
                              << "，缓存已空\n";
                }
                
                frame_stats_.frames_cached = pending_frames_.size();
                frame_stats_.Print("[接收统计] ");
            }
            
            return;
        } else {
            // 过时的帧，丢弃
            {
                std::lock_guard<std::mutex> stats_lock(frame_stats_mutex_);
                frame_stats_.frames_dropped_old++;
                frame_stats_.frames_cached = pending_frames_.size();
                std::cout << "\n>>> [帧丢弃] 过时帧 seq=" << header.frame_seq 
                          << " (期望:" << next_expected_frame_seq_ << ")\n";
                frame_stats_.Print("[接收统计] ");
            }
            return;
        }
    }
    
    // 处理当前帧（主帧）
    if (should_process) {
        // 写入文件
        if (output_opened_) {
            video_writer_->WriteFrame(frame);
        }
        
        // 回调
        if (frame_callback_) {
            frame_callback_(frame);
        }
        
        // 更新统计
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.frames_received++;
            stats_.bytes_received += frame.data.size();
            
            switch (header.frame_type) {
                case FrameType::I_FRAME:
                    stats_.i_frames_received++;
                    break;
                case FrameType::P_FRAME:
                    stats_.p_frames_received++;
                    break;
                case FrameType::B_FRAME:
                    stats_.b_frames_received++;
                    break;
                default:
                    break;
            }
        }
        
        // 更新帧统计并打印
        {
            std::lock_guard<std::mutex> stats_lock(frame_stats_mutex_);
            // 当前帧 + 缓存中连续写入的帧
            uint32_t total_written = 1 + written_count;
            frame_stats_.frames_written += total_written;
            frame_stats_.frames_cached = pending_frames_.size();
            
            // 打印统计
            std::cout << "\n>>> [帧成功] seq=" << header.frame_seq 
                      << " " << VideoCodec::FrameTypeToString(frame.type)
                      << " (" << frame.data.size() << " bytes)";
            if (written_count > 0) {
                std::cout << " + 缓存写入" << written_count << "帧";
            }
            std::cout << "\n";
            frame_stats_.Print("[接收统计] ");
        }
        
        // 打印进度（每30帧）
        if (header.frame_seq % 30 == 0) {
            std::cout << "VideoReceiver: Received frame " << header.frame_seq 
                      << " (" << VideoCodec::FrameTypeToString(frame.type)
                      << ", " << frame.data.size() << " bytes)" << std::endl;
        }
    }
}

void VideoReceiver::ReportError(const std::string& error) {
    std::cerr << "VideoReceiver Error: " << error << std::endl;
    if (error_callback_) {
        error_callback_(error);
    }
}

} // namespace VideoTransmit
