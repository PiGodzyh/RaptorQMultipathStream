/**
 * voice_receiver.cpp - 语音传输接收器实现
 * 
 * 接收 RaptorQ 解码后的音频数据，写入 WAV 文件
 */

#include "voice_receiver.h"
#include "voice_transmitter.h"  // For VoiceFrameHeader, VoiceConfigHeader
#include <iostream>
#include <cstring>
#include <algorithm>

namespace VoiceReceive {

VoiceReceiver::VoiceReceiver(std::shared_ptr<DataTransmit::UnifiedReceiver> unified_receiver)
    : unified_receiver_(unified_receiver)
    , writer_(std::make_unique<VoiceCodec::VoiceWriter>()) {
}

VoiceReceiver::~VoiceReceiver() {
    Stop();
}

bool VoiceReceiver::CreateOutput(const std::string& filepath) {
    output_path_ = filepath;
    // 写入器会在收到配置后创建
    return true;
}

bool VoiceReceiver::InitWriter() {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    if (!config_received_) {
        std::cerr << "[VoiceReceiver] Config not received yet" << std::endl;
        return false;
    }
    
    if (writer_->IsOpen()) {
        return true;  // 已经初始化
    }
    
    std::cout << "[VoiceReceiver] Creating output file with config: " 
              << sample_rate_ << "Hz, " 
              << channels_ << "ch, " 
              << bits_per_sample_ << "bit" << std::endl;
    
    if (!writer_->Create(output_path_, sample_rate_, channels_, bits_per_sample_)) {
        std::cerr << "[VoiceReceiver] Failed to create output: " << output_path_ << std::endl;
        return false;
    }
    
    return true;
}

void VoiceReceiver::Start() {
    if (running_) return;
    
    running_ = true;
    stats_.start_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::cout << "========================================" << std::endl;
    std::cout << "   Voice Receiver (File Mode)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Priority: VOICE (port 9004)" << std::endl;
    std::cout << "Output: " << output_path_ << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 设置接收回调（使用多回调注册API）
    callback_id_ = unified_receiver_->registerDecodeCallback([this](DataPriority priority, uint32_t stream_id,
                                                 const std::vector<uint8_t>& data) {
        if (priority == DataPriority::VOICE) {
            OnFrameReceived(priority, stream_id, data);
        }
    });
    
    // 启动处理线程
    process_thread_ = std::thread([this]() { ProcessLoop(); });
    
    std::cout << "[VoiceReceiver] Started, listening on VOICE priority" << std::endl;
}

void VoiceReceiver::Stop() {
    if (!running_) return;
    
    running_ = false;
    queue_cv_.notify_all();
    
    // 注销回调
    if (callback_id_ >= 0) {
        unified_receiver_->unregisterDecodeCallback(callback_id_);
        callback_id_ = -1;
    }
    
    // 等待线程结束
    if (process_thread_.joinable()) process_thread_.join();
    
    // 关闭文件
    if (writer_->IsOpen()) {
        writer_->Close();
    }
    
    // 打印统计
    auto duration_ms = (std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() - stats_.start_time_us) / 1000;
    
    std::cout << "[VoiceReceiver] Stopped" << std::endl;
    std::cout << "  Frames received: " << stats_.frames_received << std::endl;
    std::cout << "  Bytes received: " << stats_.bytes_received << std::endl;
    std::cout << "  Packets lost: " << stats_.packets_lost << std::endl;
    if (duration_ms > 0) {
        std::cout << "  Duration: " << duration_ms << " ms" << std::endl;
        std::cout << "  Bitrate: " << (stats_.bytes_received * 8 / duration_ms) << " kbps" << std::endl;
    }
}

void VoiceReceiver::OnFrameReceived(DataPriority priority, uint32_t stream_id, 
                                    const std::vector<uint8_t>& data) {
    if (!running_) return;
    
    // 处理配置包 (stream_id = 0)
    if (stream_id == 0) {
        if (ParseConfig(data)) {
            InitWriter();
        }
        return;
    }
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    receive_queue_.push(data);
    queue_cv_.notify_one();
    
    // 更新统计
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.bytes_received += data.size();
}

bool VoiceReceiver::ParseConfig(const std::vector<uint8_t>& data) {
    if (data.size() < VoiceTransmit::VoiceConfigHeader::kHeaderSize) {
        std::cerr << "[VoiceReceiver] Config packet too small" << std::endl;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    const uint8_t* ptr = data.data();
    memcpy(&sample_rate_, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    memcpy(&channels_, ptr, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(&bits_per_sample_, ptr, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(&expected_total_frames_, ptr, sizeof(uint32_t));
    
    config_received_ = true;
    
    std::cout << "[VoiceReceiver] Received config: " 
              << sample_rate_ << "Hz, " 
              << channels_ << "ch, " 
              << bits_per_sample_ << "bit, "
              << expected_total_frames_ << " frames" << std::endl;
    
    return true;
}

void VoiceReceiver::ProcessLoop() {
    std::cout << "[VoiceReceiver] Processing thread started (skip lost frames mode)" << std::endl;
    
    std::vector<uint8_t> silence_frame;  // 静音帧，等收到配置后再初始化
    
    while (running_) {
        std::vector<uint8_t> data;
        bool timeout = false;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            // 等待数据，最多等待 100ms
            timeout = !queue_cv_.wait_for(lock, std::chrono::milliseconds(100), 
                [this] { return !receive_queue_.empty() || !running_; });
            
            if (!running_) break;
            
            if (!receive_queue_.empty()) {
                data = std::move(receive_queue_.front());
                receive_queue_.pop();
            }
        }
        
        // 确保写入器已初始化
        if (!writer_->IsOpen()) {
            if (!data.empty()) {
                std::cerr << "[VoiceReceiver] Writer not initialized yet, dropping frame" << std::endl;
            }
            continue;
        }
        
        // 初始化静音帧（根据实际音频参数）
        if (silence_frame.empty() && sample_rate_ > 0) {
            size_t frame_size = (sample_rate_ * 20 / 1000) * channels_ * (bits_per_sample_ / 8);
            silence_frame.resize(frame_size, 0);
            std::cout << "[Voice] Silence frame size: " << frame_size << " bytes" << std::endl;
        }
        
        // 如果没有收到数据但缓存中有数据，检查是否需要跳过丢失帧
        if (data.empty() && !frame_buffer_.empty()) {
            // 检查最老的缓存帧是否等待太久
            auto oldest_seq = frame_buffer_.begin()->first;
            if (oldest_seq > next_write_seq_) {
                // 丢失帧数 = 期望帧 - 最老缓存帧之间的差值
                uint32_t lost_count = oldest_seq - next_write_seq_;
                if (lost_count >= 5 && !silence_frame.empty()) {  // 如果丢失超过5帧，直接跳过
                    std::cout << "[Voice] Skip " << lost_count << " lost frames, write silence" << std::endl;
                    for (uint32_t i = 0; i < lost_count && running_; i++) {
                        writer_->WritePcm(silence_frame);
                        next_write_seq_++;
                        stats_.packets_lost++;
                    }
                }
            }
            continue;
        }
        
        if (data.empty()) continue;
        
        // 解析数据包
        auto frame = ParsePacket(data);
        if (frame.pcm_data.empty()) {
            continue;
        }
        
        // 保序写入
        if (frame.seq == next_write_seq_) {
            // 期望帧，直接写入
            writer_->WriteFrame(VoiceCodec::VoiceFrame{
                frame.seq, frame.timestamp, frame.pcm_data, true
            });
            next_write_seq_++;
            
            // 检查缓存中是否有连续的帧
            while (!frame_buffer_.empty() && frame_buffer_.begin()->first == next_write_seq_) {
                auto& cached_frame = frame_buffer_.begin()->second;
                writer_->WriteFrame(VoiceCodec::VoiceFrame{
                    cached_frame.seq, cached_frame.timestamp, cached_frame.pcm_data, true
                });
                frame_buffer_.erase(frame_buffer_.begin());
                next_write_seq_++;
            }
        } else if (frame.seq > next_write_seq_) {
            // 未来帧，检查是否需要填充丢失帧
            uint32_t gap = frame.seq - next_write_seq_;
            if (gap >= 10 && frame_buffer_.size() >= 50 && !silence_frame.empty()) {
                // 间隔太大且缓存快满，跳过丢失帧，填充静音
                std::cout << "[Voice] Gap too large (" << gap << "), skip and write silence" << std::endl;
                for (uint32_t i = 0; i < gap && running_; i++) {
                    writer_->WritePcm(silence_frame);
                    next_write_seq_++;
                    stats_.packets_lost++;
                }
                // 写入当前帧
                writer_->WriteFrame(VoiceCodec::VoiceFrame{
                    frame.seq, frame.timestamp, frame.pcm_data, true
                });
                next_write_seq_ = frame.seq + 1;
            } else {
                // 正常缓存
                if (frame_buffer_.size() < 100) {
                    frame_buffer_[frame.seq] = std::move(frame);
                } else if (!silence_frame.empty()) {
                    // 缓存满，填充静音并丢弃最老的
                    stats_.packets_lost++;
                    writer_->WritePcm(silence_frame);
                    next_write_seq_++;
                    frame_buffer_.erase(frame_buffer_.begin());
                    frame_buffer_[frame.seq] = std::move(frame);
                } else {
                    // 静音帧未初始化，直接丢弃
                    frame_buffer_.erase(frame_buffer_.begin());
                    frame_buffer_[frame.seq] = std::move(frame);
                }
            }
        } else {
            // 重复帧或已跳过的帧，丢弃
        }
        
        // 更新统计
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.frames_received++;
            
            // 每100帧打印一次
            if (stats_.frames_received % 100 == 0) {
                std::cout << "[Voice] Received: " << stats_.frames_received 
                          << " frames, Buffer: " << frame_buffer_.size() << std::endl;
            }
        }
    }
    
    // 写入缓存中剩余的帧，中间缺失的用静音填充
    uint32_t last_seq = next_write_seq_;
    for (auto& pair : frame_buffer_) {
        if (!writer_->IsOpen()) break;
        
        // 填充丢失帧
        while (last_seq < pair.first && !silence_frame.empty()) {
            writer_->WritePcm(silence_frame);
            last_seq++;
            stats_.packets_lost++;
        }
        
        writer_->WriteFrame(VoiceCodec::VoiceFrame{
            pair.second.seq, pair.second.timestamp, pair.second.pcm_data, true
        });
        last_seq = pair.first + 1;
    }
    frame_buffer_.clear();
    
    std::cout << "[VoiceReceiver] Processing thread stopped" << std::endl;
}

ReceiveFrame VoiceReceiver::ParsePacket(const std::vector<uint8_t>& data) {
    ReceiveFrame frame;
    
    if (data.size() < VoiceTransmit::VoiceFrameHeader::kHeaderSize) {
        return frame;
    }
    
    const uint8_t* ptr = data.data();
    memcpy(&frame.seq, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    memcpy(&frame.timestamp, ptr, sizeof(uint64_t));
    ptr += sizeof(uint64_t);
    
    uint32_t data_size;
    memcpy(&data_size, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    // 提取 PCM 数据
    size_t pcm_size = data.size() - VoiceTransmit::VoiceFrameHeader::kHeaderSize;
    frame.pcm_data.assign(ptr, ptr + pcm_size);
    
    return frame;
}

} // namespace VoiceReceive
