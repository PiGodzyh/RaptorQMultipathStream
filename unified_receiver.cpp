#include "unified_receiver.h"

#include <iostream>
#include <cstring>

UnifiedReceiverConfig::UnifiedReceiverConfig() {
    // 默认配置
    feedback_config.feedback_interval_ms = 100;
    
    reorder_config.fc_timeout_ms = 30;
    reorder_config.voice_timeout_ms = 50;
    reorder_config.video_timeout_ms = 100;
    reorder_config.pc_timeout_ms = 500;
    reorder_config.grid_timeout_ms = 200;
}

UnifiedReceiver::UnifiedReceiver(const UnifiedReceiverConfig& config)
    : config_(config)
    , reorder_buffer_(config.reorder_config)
    , feedback_sender_(config.feedback_config) {
    callbacks_.resize(5);  // 5种优先级
}

UnifiedReceiver::~UnifiedReceiver() {
    stop();
}

bool UnifiedReceiver::initialize() {
    std::cout << "[UnifiedReceiver] Initializing..." << std::endl;
    
    // 初始化 ReorderBuffer
    reorder_buffer_.start();
    std::cout << "  ReorderBuffer initialized" << std::endl;
    
    // 创建 5 个 Receiver（监听不同端口）
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        uint16_t port = getPort(priority);
        
        auto receiver = std::make_unique<Receiver>(this, port, 2);
        receivers_.push_back(std::move(receiver));
        
        std::cout << "  Receiver created for port " << port << std::endl;
    }
    
    // 初始化 FeedbackSender
    feedback_sender_.setSendCallback([this](const FeedbackPacket& packet) {
        // 这里应该通过网络发送反馈包
        // 暂时只打印
        const char* names[] = {"FC", "Voice", "Video", "PC", "Grid"};
        std::cout << "[FeedbackSender] Sending feedback for " 
                  << names[static_cast<int>(packet.priority)]
                  << " stream " << packet.stream_id
                  << " (loss=" << (packet.loss_rate * 100) << "%)" << std::endl;
    });
    std::cout << "  FeedbackSender initialized" << std::endl;
    
    std::cout << "[UnifiedReceiver] Initialization complete" << std::endl;
    return true;
}

void UnifiedReceiver::start() {
    if (running_) return;
    
    std::cout << "[UnifiedReceiver] Starting..." << std::endl;
    
    running_ = true;
    
    // 启动所有 Receiver
    for (auto& receiver : receivers_) {
        if (receiver) {
            receiver->start();
        }
    }
    
    // 启动 FeedbackSender
    feedback_sender_.start();
    
    // 启动消费线程
    consume_thread_ = std::thread(&UnifiedReceiver::consumeLoop, this);
    
    std::cout << "[UnifiedReceiver] Started" << std::endl;
}

void UnifiedReceiver::stop() {
    if (!running_) return;
    
    std::cout << "[UnifiedReceiver] Stopping..." << std::endl;
    
    running_ = false;
    
    // 停止所有 Receiver
    for (auto& receiver : receivers_) {
        if (receiver) {
            receiver->stop();
        }
    }
    
    // 停止 FeedbackSender
    feedback_sender_.stop();
    
    // 等待消费线程
    if (consume_thread_.joinable()) {
        consume_thread_.join();
    }
    
    // 停止 ReorderBuffer
    reorder_buffer_.stop();
    
    std::cout << "[UnifiedReceiver] Stopped" << std::endl;
}

void UnifiedReceiver::OnDecodeComplete(uint32_t stream_id, 
                                       const std::vector<uint8_t>& data) {
    // 提取优先级（简化：假设数据头部包含优先级信息）
    DataPriority priority = extractPriority(data);
    
    // 生成序列号（简化：使用当前计数）
    static std::map<uint64_t, uint64_t> seq_counters;
    uint64_t seq = seq_counters[stream_id]++;
    
    // 插入 ReorderBuffer
    reorder_buffer_.insert(priority, stream_id, seq, 
                          std::vector<uint8_t>(data));
    
    // 报告给 FeedbackSender
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    feedback_sender_.reportSymbolReceived(stream_id, priority, seq, now_ms);
}

void UnifiedReceiver::consumeLoop() {
    std::cout << "[UnifiedReceiver] Consume thread started" << std::endl;
    
    while (running_) {
        ReorderUnit unit;
        
        // 从 ReorderBuffer 取有序数据
        if (reorder_buffer_.pop(unit)) {
            int idx = static_cast<int>(unit.priority);
            
            // 调用回调
            if (idx >= 0 && idx < 5 && callbacks_[idx]) {
                callbacks_[idx](unit.priority, unit.stream_id, unit.data);
            } else if (unified_callback_) {
                unified_callback_(unit.priority, unit.stream_id, unit.data);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    std::cout << "[UnifiedReceiver] Consume thread stopped" << std::endl;
}

void UnifiedReceiver::setDataCallback(DataPriority priority, DataCallback callback) {
    int idx = static_cast<int>(priority);
    if (idx >= 0 && idx < 5) {
        callbacks_[idx] = callback;
    }
}

void UnifiedReceiver::setDataCallback(DataCallback callback) {
    unified_callback_ = callback;
}

void UnifiedReceiver::sendFeedback(const FeedbackPacket& packet) {
    // 手动触发反馈发送
    feedback_sender_.sendFeedbackNow(packet.stream_id);
}

DataPriority UnifiedReceiver::extractPriority(const std::vector<uint8_t>& data) const {
    // 简化：从 stream_id 或数据头部提取
    // 实际实现应该在数据头部添加类型标记
    if (data.size() >= 4) {
        // 假设头部第一个字节是优先级
        uint8_t prio = data[0];
        if (prio < 5) {
            return static_cast<DataPriority>(prio);
        }
    }
    return DataPriority::VIDEO;  // 默认视频
}

uint16_t UnifiedReceiver::getPort(DataPriority priority) const {
    switch (priority) {
        case DataPriority::FC_COMMAND: return 9000;
        case DataPriority::VOICE: return 9004;
        case DataPriority::VIDEO: return 9001;
        case DataPriority::POINT_CLOUD: return 9002;
        case DataPriority::GRID_MAP: return 9003;
        default: return 9000;
    }
}

void UnifiedReceiver::printStatistics() const {
    std::cout << "\n========== UnifiedReceiver Statistics ==========" << std::endl;
    
    reorder_buffer_.printStatistics();
    
    auto fb_stats = feedback_sender_.getStatistics();
    std::cout << "Feedback sent: " << fb_stats.feedback_sent << std::endl;
    std::cout << "Symbols reported: " << fb_stats.symbols_reported << std::endl;
    
    std::cout << "===============================================\n" << std::endl;
}
