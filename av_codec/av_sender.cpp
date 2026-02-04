/**
 * @file av_sender.cpp
 * @brief 音视频发送器实现
 */

#include "av_sender.h"
#include <iostream>
#include <chrono>
#include <cstring>

namespace AVCodecModule {

// UDP 最大安全载荷大小（避免分片）
static const size_t MAX_UDP_PAYLOAD = 1400;

// 分片包头格式：
// [magic(4)] [packet_index(4)] [fragment_index(2)] [fragment_count(2)] [data_size(4)] [data...]
static const uint32_t FRAGMENT_MAGIC = 0x46524147;  // "FRAG"
static const size_t FRAGMENT_HEADER_SIZE = 16;

AVSender::AVSender(const std::string& server_addr, uint16_t server_port)
    : server_addr_(server_addr)
    , server_port_(server_port)
{
    client_.setDefaultTarget(server_addr, server_port);
}

AVSender::~AVSender() {
    stop();
    close();
}

bool AVSender::open(const std::string& filename) {
    return reader_.open(filename);
}

void AVSender::close() {
    reader_.close();
}

bool AVSender::sendFragmented(const std::vector<uint8_t>& data, uint32_t packet_index) {
    size_t max_fragment_data = MAX_UDP_PAYLOAD - FRAGMENT_HEADER_SIZE;
    size_t total_fragments = (data.size() + max_fragment_data - 1) / max_fragment_data;
    
    if (total_fragments > 65535) {
        std::cerr << "数据包太大，无法分片" << std::endl;
        return false;
    }
    
    for (size_t i = 0; i < total_fragments; i++) {
        size_t offset = i * max_fragment_data;
        size_t fragment_size = std::min(max_fragment_data, data.size() - offset);
        
        // 构建分片包
        std::vector<uint8_t> fragment_packet;
        fragment_packet.reserve(FRAGMENT_HEADER_SIZE + fragment_size);
        
        // Magic
        fragment_packet.push_back(FRAGMENT_MAGIC & 0xFF);
        fragment_packet.push_back((FRAGMENT_MAGIC >> 8) & 0xFF);
        fragment_packet.push_back((FRAGMENT_MAGIC >> 16) & 0xFF);
        fragment_packet.push_back((FRAGMENT_MAGIC >> 24) & 0xFF);
        
        // Packet index
        fragment_packet.push_back(packet_index & 0xFF);
        fragment_packet.push_back((packet_index >> 8) & 0xFF);
        fragment_packet.push_back((packet_index >> 16) & 0xFF);
        fragment_packet.push_back((packet_index >> 24) & 0xFF);
        
        // Fragment index (2 bytes)
        fragment_packet.push_back(i & 0xFF);
        fragment_packet.push_back((i >> 8) & 0xFF);
        
        // Fragment count (2 bytes)
        fragment_packet.push_back(total_fragments & 0xFF);
        fragment_packet.push_back((total_fragments >> 8) & 0xFF);
        
        // Data size (4 bytes) - 原始数据总大小
        uint32_t total_size = static_cast<uint32_t>(data.size());
        fragment_packet.push_back(total_size & 0xFF);
        fragment_packet.push_back((total_size >> 8) & 0xFF);
        fragment_packet.push_back((total_size >> 16) & 0xFF);
        fragment_packet.push_back((total_size >> 24) & 0xFF);
        
        // Data
        fragment_packet.insert(fragment_packet.end(), 
                              data.begin() + offset,
                              data.begin() + offset + fragment_size);
        
        // 发送
        ssize_t sent = client_.send(fragment_packet);
        if (sent < 0) {
            std::cerr << "发送分片 " << i << "/" << total_fragments << " 失败" << std::endl;
            return false;
        }
        
        sent_bytes_ += sent;
        
        // 分片间短暂延时，避免拥塞
        if (i < total_fragments - 1) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    return true;
}

bool AVSender::sendHeader() {
    // 获取媒体头
    MediaHeader header = reader_.getMediaHeader();
    std::vector<uint8_t> header_data = header.serialize();
    
    // 用特殊标记封装头部（index = 0xFFFFFFFF 表示头部）
    MediaPacket header_pkt;
    header_pkt.type = FrameType::UNKNOWN;
    header_pkt.index = 0xFFFFFFFF;
    header_pkt.data = header_data;
    
    std::vector<uint8_t> serialized = header_pkt.serialize();
    
    // 发送（可能需要分片）
    if (serialized.size() > MAX_UDP_PAYLOAD) {
        if (!sendFragmented(serialized, 0xFFFFFFFF)) {
            std::cerr << "发送媒体头失败" << std::endl;
            return false;
        }
    } else {
        ssize_t sent = client_.send(serialized);
        if (sent < 0) {
            std::cerr << "发送媒体头失败" << std::endl;
            return false;
        }
        sent_bytes_ += sent;
    }
    
    std::cout << "✓ 媒体头已发送 (" << serialized.size() << " 字节)" << std::endl;
    return true;
}

bool AVSender::sendPacket(const MediaPacket& packet) {
    std::vector<uint8_t> data = packet.serialize();
    
    // 大包分片发送
    if (data.size() > MAX_UDP_PAYLOAD) {
        if (!sendFragmented(data, packet.index)) {
            return false;
        }
    } else {
        ssize_t sent = client_.send(data);
        if (sent < 0) {
            return false;
        }
        sent_bytes_ += sent;
    }
    
    sent_packets_++;
    return true;
}

bool AVSender::sendAll() {
    if (!reader_.isOpen()) {
        std::cerr << "媒体文件未打开" << std::endl;
        return false;
    }
    
    running_ = true;
    sent_packets_ = 0;
    sent_bytes_ = 0;
    
    // 发送媒体头
    if (!sendHeader()) {
        running_ = false;
        return false;
    }
    
    // 等待头部到达
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 发送所有数据包
    MediaPacket packet;
    uint64_t video_count = 0;
    uint64_t audio_count = 0;
    uint64_t failed_count = 0;
    
    while (running_ && reader_.readPacket(packet)) {
        if (!sendPacket(packet)) {
            failed_count++;
            if (failed_count % 100 == 1) {
                std::cerr << "\n发送包 #" << packet.index << " 失败 (总失败: " << failed_count << ")" << std::endl;
            }
            continue;
        }
        
        if (packet.type == FrameType::VIDEO) {
            video_count++;
            if (video_count % 30 == 0) {
                std::cout << "\r发送进度: 视频=" << video_count 
                          << ", 音频=" << audio_count 
                          << ", 失败=" << failed_count << "        " << std::flush;
            }
        } else if (packet.type == FrameType::AUDIO) {
            audio_count++;
        }
        
        // 进度回调
        if (progress_callback_) {
            progress_callback_(sent_packets_, sent_bytes_);
        }
        
        // 发送间隔
        if (send_interval_ms_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(send_interval_ms_));
        } else {
            // 默认短暂延时避免发送过快
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
    
    std::cout << std::endl;
    
    // 发送结束标记
    std::cout << "发送结束标记..." << std::endl;
    MediaPacket eos_packet;
    eos_packet.type = FrameType::END_OF_STREAM;
    eos_packet.index = static_cast<uint32_t>(video_count + audio_count);
    eos_packet.pts = 0;
    eos_packet.dts = 0;
    eos_packet.is_key_frame = false;
    
    // 发送多次确保接收端收到
    for (int i = 0; i < 3; i++) {
        if (sendPacket(eos_packet)) {
            std::cout << "已发送结束标记 (" << (i + 1) << "/3)" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    running_ = false;
    
    std::cout << "发送完成: 视频=" << video_count << ", 音频=" << audio_count 
              << ", 失败=" << failed_count
              << ", 总字节=" << (sent_bytes_ / 1024.0 / 1024.0) << " MB" << std::endl;
    
    return true;
}

void AVSender::startAsync() {
    if (running_) return;
    
    send_thread_ = std::thread([this]() {
        sendAll();
    });
}

void AVSender::stop() {
    running_ = false;
    
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
}

} // namespace AVCodecModule
