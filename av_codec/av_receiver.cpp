/**
 * @file av_receiver.cpp
 * @brief 音视频接收器实现
 */

#include "av_receiver.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

namespace AVCodecModule {

// 分片包头格式：
// [magic(4)] [packet_index(4)] [fragment_index(2)] [fragment_count(2)] [data_size(4)] [data...]
static const uint32_t FRAGMENT_MAGIC = 0x46524147;  // "FRAG"
static const size_t FRAGMENT_HEADER_SIZE = 16;
static const int FRAGMENT_TIMEOUT_SEC = 5;  // 分片超时时间

AVReceiver::AVReceiver(uint16_t port)
    : port_(port)
    , server_(port)
{
}

AVReceiver::~AVReceiver() {
    stop();
    finalize();
}

bool AVReceiver::tryReassemble(uint32_t packet_index, FragmentInfo& info, std::vector<uint8_t>& result) {
    if (info.fragments.size() != info.fragment_count) {
        return false;
    }
    
    // 所有分片已收到，重组
    result.clear();
    result.reserve(info.total_size);
    
    for (uint16_t i = 0; i < info.fragment_count; i++) {
        auto it = info.fragments.find(i);
        if (it == info.fragments.end()) {
            return false;  // 不应该发生
        }
        result.insert(result.end(), it->second.begin(), it->second.end());
    }
    
    return true;
}

void AVReceiver::cleanupOldFragments() {
    auto now = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(fragment_mutex_);
    
    for (auto it = fragment_buffers_.begin(); it != fragment_buffers_.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.first_time).count();
        
        if (elapsed > FRAGMENT_TIMEOUT_SEC) {
            it = fragment_buffers_.erase(it);
        } else {
            ++it;
        }
    }
}

void AVReceiver::onReceive(std::shared_ptr<Network::Packet> packet) {
    if (!packet || packet->data.empty()) {
        return;
    }
    
    received_bytes_ += packet->data.size();
    
    const std::vector<uint8_t>& data = packet->data;
    
    // 检查是否是分片包
    if (data.size() >= FRAGMENT_HEADER_SIZE) {
        uint32_t magic = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
        
        if (magic == FRAGMENT_MAGIC) {
            // 这是分片包
            uint32_t packet_index = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
            uint16_t fragment_index = data[8] | (data[9] << 8);
            uint16_t fragment_count = data[10] | (data[11] << 8);
            uint32_t total_size = data[12] | (data[13] << 8) | (data[14] << 16) | (data[15] << 24);
            
            // 提取分片数据
            std::vector<uint8_t> fragment_data(data.begin() + FRAGMENT_HEADER_SIZE, data.end());
            
            std::vector<uint8_t> complete_data;
            bool is_complete = false;
            
            {
                std::lock_guard<std::mutex> lock(fragment_mutex_);
                
                FragmentInfo& info = fragment_buffers_[packet_index];
                
                if (info.fragments.empty()) {
                    // 第一个分片
                    info.total_size = total_size;
                    info.fragment_count = fragment_count;
                    info.first_time = std::chrono::steady_clock::now();
                }
                
                // 存储分片
                info.fragments[fragment_index] = std::move(fragment_data);
                
                // 尝试重组
                if (tryReassemble(packet_index, info, complete_data)) {
                    is_complete = true;
                    fragment_buffers_.erase(packet_index);
                }
            }
            
            if (is_complete) {
                processCompleteData(complete_data);
            }
            
            // 定期清理超时分片
            static uint64_t cleanup_counter = 0;
            if (++cleanup_counter % 100 == 0) {
                cleanupOldFragments();
            }
            
            return;
        }
    }
    
    // 不是分片包，直接处理
    processCompleteData(data);
}

void AVReceiver::processCompleteData(const std::vector<uint8_t>& data) {
    // 反序列化 MediaPacket
    MediaPacket media_pkt;
    if (!MediaPacket::deserialize(data, media_pkt)) {
        // 可能是旧格式或损坏的数据
        return;
    }
    
    // 检查是否是媒体头（特殊标记 index = 0xFFFFFFFF）
    if (media_pkt.index == 0xFFFFFFFF) {
        MediaHeader header;
        if (!MediaHeader::deserialize(media_pkt.data, header)) {
            std::cerr << "无法解析媒体头" << std::endl;
            return;
        }
        
        std::lock_guard<std::mutex> lock(writer_mutex_);
        
        if (!header_received_) {
            // 初始化写入器
            if (!writer_.initialize(header)) {
                std::cerr << "初始化写入器失败" << std::endl;
                return;
            }
            
            if (!output_file_.empty()) {
                if (!writer_.open(output_file_)) {
                    std::cerr << "打开输出文件失败: " << output_file_ << std::endl;
                    return;
                }
                writer_ready_ = true;
            }
            
            header_received_ = true;
            
            std::cout << "\n✓ 收到媒体头" << std::endl;
            if (header.info.has_video) {
                std::cout << "  视频: " << header.info.video_width 
                          << "x" << header.info.video_height << std::endl;
            }
            if (header.info.has_audio) {
                std::cout << "  音频: " << header.info.audio_sample_rate 
                          << "Hz, " << header.info.audio_channels 
                          << " 声道" << std::endl;
            }
            std::cout << std::endl;
        }
        return;
    }
    
    // 检查是否是结束标记
    if (media_pkt.type == FrameType::END_OF_STREAM) {
        std::cout << "\n✓ 收到结束标记" << std::endl;
        
        // 设置标志，让主循环安全退出（不在回调中直接调用 stop）
        running_ = false;
        eos_received_ = true;
        
        return;
    }
    
    // 普通媒体包
    received_packets_++;
    processPacket(media_pkt);
    
    // 进度回调
    if (progress_callback_) {
        progress_callback_(received_packets_, received_bytes_);
    }
}

void AVReceiver::processPacket(const MediaPacket& packet) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    // 添加到缓冲区
    if (packet_buffer_.size() >= max_buffer_size_) {
        // 缓冲区满，检查是否有旧的包可以弹出
        const MediaPacket& top = packet_buffer_.top();
        if (top.index < next_expected_index_ - 100) {
            // 太旧的包，丢弃
            packet_buffer_.pop();
        } else {
            // 缓冲区满但都是新包，强制弹出最小的
            packet_buffer_.pop();
        }
    }
    packet_buffer_.push(packet);
    
    // 如果写入器未就绪，只缓存
    if (!writer_ready_) {
        return;
    }
    
    // 处理可以写入的包
    std::lock_guard<std::mutex> writer_lock(writer_mutex_);
    
    // 尝试写入所有可以写入的包（按顺序）
    while (!packet_buffer_.empty()) {
        const MediaPacket& top = packet_buffer_.top();
        
        // 如果是期望的下一个包，直接写入
        if (top.index == next_expected_index_) {
            MediaPacket pkt = top;
            packet_buffer_.pop();
            
            if (writer_.writePacket(pkt)) {
                next_expected_index_ = pkt.index + 1;
            } else {
                // 写入失败，但继续处理（可能是数据问题）
                std::cerr << "\n警告: 写入包 #" << pkt.index << " 失败，跳过" << std::endl;
                next_expected_index_ = pkt.index + 1;  // 跳过这个包
                continue;  // 继续处理下一个
            }
        } 
        // 如果等待太久（缓冲区很大），强制写入最小的包
        else if (packet_buffer_.size() >= max_wait_packets_) {
            MediaPacket pkt = top;
            packet_buffer_.pop();
            
            if (pkt.index < next_expected_index_) {
                // 这是旧包，已经跳过，直接丢弃
                continue;
            }
            
            if (writer_.writePacket(pkt)) {
                // 更新期望索引（跳过丢失的包）
                std::cout << "\n跳过包 " << next_expected_index_ << " 到 " << pkt.index - 1 << std::endl;
                next_expected_index_ = pkt.index + 1;
            } else {
                // 写入失败，跳过
                std::cerr << "\n警告: 写入包 #" << pkt.index << " 失败，跳过" << std::endl;
                next_expected_index_ = pkt.index + 1;
                continue;
            }
        } else {
            // 等待更多包
            break;
        }
    }
    
    // 定期输出进度
    static uint64_t last_print = 0;
    if (writer_.getPacketCount() - last_print >= 30) {
        std::cout << "\r接收进度: 已写入=" << writer_.getPacketCount() 
                  << ", 期望=" << next_expected_index_
                  << ", 缓冲=" << packet_buffer_.size() 
                  << ", 分片=" << fragment_buffers_.size()
                  << "        " << std::flush;
        last_print = writer_.getPacketCount();
    }
}

void AVReceiver::flushBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    std::lock_guard<std::mutex> writer_lock(writer_mutex_);
    
    if (!writer_ready_) {
        return;
    }
    
    // 将缓冲区中的所有包按顺序写入
    std::vector<MediaPacket> remaining;
    while (!packet_buffer_.empty()) {
        remaining.push_back(packet_buffer_.top());
        packet_buffer_.pop();
    }
    
    // 按 index 排序
    std::sort(remaining.begin(), remaining.end(),
        [](const MediaPacket& a, const MediaPacket& b) {
            return a.index < b.index;
        });
    
    uint64_t written = 0;
    uint64_t skipped = 0;
    
    for (const auto& pkt : remaining) {
        if (writer_.writePacket(pkt)) {
            written++;
            if (pkt.index >= next_expected_index_) {
                next_expected_index_ = pkt.index + 1;
            }
        } else {
            skipped++;
            std::cerr << "\n刷新时写入包 #" << pkt.index << " 失败" << std::endl;
        }
    }
    
    if (written > 0 || skipped > 0) {
        std::cout << "\n刷新缓冲区: 写入 " << written << " 包";
        if (skipped > 0) {
            std::cout << ", 跳过 " << skipped << " 包";
        }
        std::cout << std::endl;
    }
}

bool AVReceiver::start() {
    if (output_file_.empty()) {
        std::cerr << "请先设置输出文件" << std::endl;
        return false;
    }
    
    running_ = true;
    received_packets_ = 0;
    received_bytes_ = 0;
    next_expected_index_ = 0;
    finalized_ = false;
    header_received_ = false;
    writer_ready_ = false;
    eos_received_ = false;
    
    // 设置接收回调
    server_.setReceiveCallback([this](std::shared_ptr<Network::Packet> packet) {
        onReceive(packet);
    });
    
    server_.setErrorCallback([](const std::string& error) {
        std::cerr << "网络错误: " << error << std::endl;
    });
    
    std::cout << "接收器启动，监听端口 " << port_ << std::endl;
    std::cout << "等待接收音视频数据..." << std::endl;
    std::cout << "按 Ctrl+C 停止" << std::endl << std::endl;
    
    // 使用异步启动，然后在主线程中等待
    server_.startAsync();
    
    // 等待结束标记或用户中断
    while (running_ && !eos_received_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 收到结束标记，安全停止
    if (eos_received_) {
        std::cout << "正在完成文件写入..." << std::endl;
        server_.stop();
        finalize();
    }
    
    running_ = false;
    return true;
}

void AVReceiver::startAsync() {
    if (output_file_.empty()) {
        std::cerr << "请先设置输出文件" << std::endl;
        return;
    }
    
    running_ = true;
    received_packets_ = 0;
    received_bytes_ = 0;
    next_expected_index_ = 0;
    finalized_ = false;
    header_received_ = false;
    writer_ready_ = false;
    eos_received_ = false;
    
    // 设置接收回调
    server_.setReceiveCallback([this](std::shared_ptr<Network::Packet> packet) {
        onReceive(packet);
    });
    
    server_.setErrorCallback([](const std::string& error) {
        std::cerr << "网络错误: " << error << std::endl;
    });
    
    std::cout << "接收器启动，监听端口 " << port_ << std::endl;
    
    server_.startAsync();
}

void AVReceiver::stop() {
    if (!running_) {
        return;  // 已经停止
    }
    
    std::cout << "\n正在停止接收..." << std::endl;
    running_ = false;
    server_.stop();
    
    // 等待一段时间，让所有正在处理的数据包完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 处理剩余的分片
    {
        std::lock_guard<std::mutex> lock(fragment_mutex_);
        for (auto it = fragment_buffers_.begin(); it != fragment_buffers_.end(); ) {
            std::vector<uint8_t> complete_data;
            if (tryReassemble(it->first, it->second, complete_data)) {
                processCompleteData(complete_data);
                it = fragment_buffers_.erase(it);
            } else {
                it++;
            }
        }
    }
    
    // 再次等待，确保所有数据都处理完成
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    std::cout << "接收已停止" << std::endl;
}

void AVReceiver::finalize() {
    // 防止重复调用
    bool expected = false;
    if (!finalized_.compare_exchange_strong(expected, true)) {
        std::cout << "finalize() 已被调用，跳过" << std::endl;
        return;
    }
    
    // 等待一段时间，让所有分片到达
    std::cout << "\n等待分片重组..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 尝试处理剩余的分片
    {
        std::lock_guard<std::mutex> lock(fragment_mutex_);
        
        for (auto it = fragment_buffers_.begin(); it != fragment_buffers_.end(); ) {
            std::vector<uint8_t> complete_data;
            if (tryReassemble(it->first, it->second, complete_data)) {
                // 成功重组，处理数据
                processCompleteData(complete_data);
                it = fragment_buffers_.erase(it);
            } else {
                // 分片不完整，记录并丢弃
                std::cerr << "警告: 包 #" << it->first << " 的分片不完整 ("
                          << it->second.fragments.size() << "/" << it->second.fragment_count 
                          << ")，已丢弃" << std::endl;
                it = fragment_buffers_.erase(it);
            }
        }
    }
    
    // 再次等待
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 刷新剩余缓冲
    flushBuffer();
    
    // 完成写入
    if (writer_ready_) {
        std::lock_guard<std::mutex> lock(writer_mutex_);
        
        uint64_t packets_before = writer_.getPacketCount();
        
        writer_.finalize();
        writer_.close();
        writer_ready_ = false;
        
        uint64_t packets_after = writer_.getPacketCount();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "✓ 输出文件已保存: " << output_file_ << std::endl;
        std::cout << "  接收包数: " << received_packets_ << std::endl;
        std::cout << "  写入包数: " << packets_after << std::endl;
        std::cout << "  视频包: " << writer_.getVideoPacketCount() << std::endl;
        std::cout << "  音频包: " << writer_.getAudioPacketCount() << std::endl;
        std::cout << "  关键帧: " << writer_.getKeyFrameCount() << std::endl;
        std::cout << "  期望包索引: " << next_expected_index_ << std::endl;
        std::cout << "  接收字节: " << (received_bytes_ / 1024.0 / 1024.0) << " MB" << std::endl;
        
        // 检查缓冲区
        {
            std::lock_guard<std::mutex> buf_lock(buffer_mutex_);
            if (!packet_buffer_.empty()) {
                std::cout << "  警告: 缓冲区中仍有 " << packet_buffer_.size() << " 个包未写入" << std::endl;
            }
        }
        
        if (packets_after < received_packets_) {
            std::cout << "  警告: 有 " << (received_packets_ - packets_after) 
                      << " 个包未写入（可能丢失或损坏）" << std::endl;
        }
        
        // 检查分片缓冲区
        {
            std::lock_guard<std::mutex> frag_lock(fragment_mutex_);
            if (!fragment_buffers_.empty()) {
                std::cout << "  警告: 仍有 " << fragment_buffers_.size() 
                          << " 个不完整的分片包" << std::endl;
            }
        }
        
        // 检查文件是否有效
        if (packets_after == 0) {
            std::cout << "  错误: 没有写入任何包，文件可能无效！" << std::endl;
        } else if (packets_after < 10) {
            std::cout << "  警告: 写入的包数很少，文件可能不完整！" << std::endl;
        }
        
        // 检查关键帧
        if (writer_.getVideoPacketCount() > 0 && writer_.getKeyFrameCount() == 0) {
            std::cout << "  错误: 没有关键帧，文件无法播放！" << std::endl;
        } else if (writer_.getVideoPacketCount() > 100 && writer_.getKeyFrameCount() < 3) {
            std::cout << "  警告: 关键帧太少，文件可能无法正常播放！" << std::endl;
        }
        
        std::cout << "========================================" << std::endl;
    }
}

} // namespace AVCodecModule
