/**
 * voice_transmitter.cpp - 语音传输发送器实现
 * 
 * 从文件读取音频，RaptorQ 编码后发送
 */

#include "voice_transmitter.h"
#include "pack/rq_pack.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

namespace VoiceTransmit {

VoiceTransmitter::VoiceTransmitter(const std::string& server_addr, uint16_t server_port)
    : sender_(std::make_unique<Sender>(server_addr, server_port, 256, 1))  // 256B符号，单线程编码
    , reader_(std::make_unique<VoiceCodec::VoiceReader>())
    , server_addr_(server_addr)
    , server_port_(server_port) {
}

VoiceTransmitter::VoiceTransmitter(std::shared_ptr<UnifiedSender> unified_sender)
    : unified_sender_(unified_sender)
    , reader_(std::make_unique<VoiceCodec::VoiceReader>())
    , server_addr_("shared")
    , server_port_(9004) {
}

VoiceTransmitter::~VoiceTransmitter() {
    Stop();
}

bool VoiceTransmitter::OpenFile(const std::string& filepath) {
    filepath_ = filepath;
    
    if (!reader_->Open(filepath)) {
        std::cerr << "[VoiceTransmitter] Failed to open: " << filepath << std::endl;
        return false;
    }
    
    return true;
}

void VoiceTransmitter::Start() {
    if (running_ || !reader_->IsOpen()) return;
    
    running_ = true;
    stats_.start_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    std::cout << "========================================" << std::endl;
    std::cout << "   Voice Transmitter (File Mode)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Target: " << server_addr_ << ":" << server_port_ << std::endl;
    std::cout << "File: " << filepath_ << std::endl;
    std::cout << "Total frames: " << reader_->GetTotalFrames() << std::endl;
    std::cout << "Frame interval: " << VoiceCodec::kFrameDurationMs << "ms" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 启动独立Sender（UnifiedSender模式下由外部管理）
    if (sender_) {
        sender_->start();
        sender_->setSendInterval(100);  // 100微秒，最小延迟
    }
    
    // 启动发送线程
    transmit_thread_ = std::thread([this]() { TransmitLoop(); });
    
    std::cout << "[VoiceTransmitter] Started" << std::endl;
}

void VoiceTransmitter::Stop() {
    if (!running_) return;
    
    running_ = false;
    
    // 等待发送线程结束
    if (transmit_thread_.joinable()) {
        transmit_thread_.join();
    }
    
    // 停止独立Sender（UnifiedSender模式下由外部管理）
    if (sender_) {
        sender_->stop();
    }
    
    // 关闭文件
    reader_->Close();
    
    // 打印统计
    auto duration_ms = (std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() - stats_.start_time_us) / 1000;
    
    std::cout << "[VoiceTransmitter] Stopped" << std::endl;
    std::cout << "  Frames sent: " << stats_.frames_sent << std::endl;
    std::cout << "  Bytes sent: " << stats_.bytes_sent << std::endl;
    if (duration_ms > 0) {
        std::cout << "  Duration: " << duration_ms << " ms" << std::endl;
        std::cout << "  Bitrate: " << (stats_.bytes_sent * 8 / duration_ms) << " kbps" << std::endl;
    }
}

void VoiceTransmitter::TransmitLoop() {
    std::cout << "[VoiceTransmitter] Transmit loop started" << std::endl;
    
    const uint32_t total_frames = reader_->GetTotalFrames();
    auto start_time = std::chrono::steady_clock::now();
    
    // 首先发送音频配置（stream_id=0）
    SendConfig();
    
    // 按帧率发送：20ms/帧
    auto next_frame_time = std::chrono::steady_clock::now();
    const auto frame_interval = std::chrono::milliseconds(VoiceCodec::kFrameDurationMs);
    
    while (running_ && !reader_->IsEndOfFile()) {
        // 读取一帧
        auto frame = reader_->ReadFrame();
        
        if (!frame.is_valid) {
            break;
        }
        
        // 发送帧
        SendFrame(frame);
        
        // 更新统计
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.frames_sent++;
            
            // 每100帧打印一次进度
            if (stats_.frames_sent % 100 == 0 || stats_.frames_sent == total_frames) {
                int progress = (stats_.frames_sent * 100) / total_frames;
                std::cout << "[Voice] Progress: " << stats_.frames_sent << "/" << total_frames 
                          << " (" << progress << "%)" << std::endl;
            }
        }
        
        // 按帧率间隔发送，避免全速读取导致发送不均匀
        next_frame_time += frame_interval;
        std::this_thread::sleep_until(next_frame_time);
    }
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    
    std::cout << "[VoiceTransmitter] Transmit loop finished in " << duration << " ms" << std::endl;
    
    // 等待 UnifiedSender 内部队列排空
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    running_ = false;
}

void VoiceTransmitter::SendConfig() {
    // 构建配置包
    VoiceConfigHeader config;
    config.sample_rate = reader_->GetSampleRate();
    config.channels = reader_->GetChannels();
    config.bits_per_sample = reader_->GetBitsPerSample();
    config.total_frames = reader_->GetTotalFrames();
    
    std::vector<uint8_t> packet(VoiceConfigHeader::kHeaderSize);
    uint8_t* ptr = packet.data();
    memcpy(ptr, &config.sample_rate, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    memcpy(ptr, &config.channels, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(ptr, &config.bits_per_sample, sizeof(uint16_t));
    ptr += sizeof(uint16_t);
    memcpy(ptr, &config.total_frames, sizeof(uint32_t));
    
    std::cout << "[VoiceTransmitter] Sending config: " 
              << config.sample_rate << "Hz, " 
              << config.channels << "ch, " 
              << config.bits_per_sample << "bit, "
              << config.total_frames << " frames" << std::endl;
    
    // RaptorQ 编码
    uint16_t symbol_size = 256;
    float redundancy = 0.05f;
    
    RQPack::Encoder encoder(packet.data(), packet.size(), symbol_size);
    uint32_t source_count = encoder.getSourceSymbolCount();
    uint32_t repair_count = static_cast<uint32_t>(source_count * redundancy);
    repair_count = std::max(1u, repair_count);
    auto symbols = encoder.encodeAll(repair_count);
    
    if (unified_sender_) {
        unified_sender_->send(DataPriority::VOICE, 0, packet);
    } else if (sender_) {
        sender_->sendSymbols(0, symbols, packet.size(), symbol_size);
    }
}

void VoiceTransmitter::SendFrame(const VoiceCodec::VoiceFrame& audio_frame) {
    // 构建网络包
    auto packet = BuildPacket(audio_frame);
    
    uint32_t stream_id = audio_frame.seq + 1;  // stream_id 从1开始
    bool sent = false;
    
    if (unified_sender_) {
        // UnifiedSender 模式下：直接发送原始数据，内部会自动进行 RaptorQ 编码和调度
        // 跳过冗余的本地 RaptorQ 编码，避免 CPU 浪费
        sent = unified_sender_->send(DataPriority::VOICE, stream_id, packet);
    } else if (sender_) {
        // 独立 Sender 模式下：需要本地进行 RaptorQ 编码
        uint16_t symbol_size = 256;
        float redundancy = 0.05f;
        
        RQPack::Encoder encoder(packet.data(), packet.size(), symbol_size);
        uint32_t source_count = encoder.getSourceSymbolCount();
        uint32_t repair_count = static_cast<uint32_t>(source_count * redundancy);
        repair_count = std::max(1u, repair_count);
        auto symbols = encoder.encodeAll(repair_count);
        
        sent = sender_->sendSymbols(stream_id, symbols, packet.size(), symbol_size);
    }
    
    if (sent) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.bytes_sent += packet.size();
    }
}

std::vector<uint8_t> VoiceTransmitter::BuildPacket(const VoiceCodec::VoiceFrame& audio_frame) {
    // 构建头部 + PCM 数据
    std::vector<uint8_t> packet;
    
    // 头部
    VoiceFrameHeader header;
    header.seq = audio_frame.seq;
    header.timestamp = audio_frame.timestamp_us;
    header.data_size = audio_frame.pcm_data.size();
    
    // 序列化
    packet.resize(VoiceFrameHeader::kHeaderSize + audio_frame.pcm_data.size());
    
    uint8_t* ptr = packet.data();
    memcpy(ptr, &header.seq, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    memcpy(ptr, &header.timestamp, sizeof(uint64_t));
    ptr += sizeof(uint64_t);
    memcpy(ptr, &header.data_size, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    
    // PCM 数据
    memcpy(ptr, audio_frame.pcm_data.data(), audio_frame.pcm_data.size());
    
    return packet;
}

uint32_t VoiceTransmitter::GetTotalFrames() const {
    return reader_->GetTotalFrames();
}

uint32_t VoiceTransmitter::GetCurrentFrame() const {
    return reader_->GetCurrentFrame();
}

} // namespace VoiceTransmit
