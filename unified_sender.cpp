#include "unified_sender.h"

#include <iostream>
#include <thread>
#include <chrono>

// 默认配置
UnifiedSenderConfig::UnifiedSenderConfig() {
    // BlockPartition 默认配置
    block_policy.max_block_size = 64 * 1024;
    block_policy.min_block_size = 1 * 1024;
    block_policy.target_block_size = 32 * 1024;
    block_policy.max_aggregation = 1;  // 禁用聚合，兼容旧接收端
    block_policy.aggregation_timeout_ms = 10;
    
    // Scheduler 默认配置：加权轮询
    scheduler_config.algorithm = ScheduleAlgorithm::WEIGHTED_ROUND_ROBIN;
    scheduler_config.fc_weight = 100;
    scheduler_config.voice_weight = 80;
    scheduler_config.video_weight = 60;
    scheduler_config.grid_map_weight = 70;
    scheduler_config.point_cloud_weight = 40;
    
    // 带宽限制
    scheduler_config.fc_bandwidth_kbps = 100;
    scheduler_config.voice_bandwidth_kbps = 500;
    scheduler_config.video_bandwidth_kbps = 6000;
    scheduler_config.grid_map_bandwidth_kbps = 2000;
    scheduler_config.point_cloud_bandwidth_kbps = 1500;
}

UnifiedSender::UnifiedSender(const UnifiedSenderConfig& config)
    : config_(config)
    , block_partition_(config.block_policy)
    , scheduler_(config.scheduler_config)
    , feedback_controller_(config.fec_config) {}

UnifiedSender::~UnifiedSender() {
    stop();
}

bool UnifiedSender::initialize() {
    std::cout << "[UnifiedSender] Initializing..." << std::endl;
    
    // 1. 初始化 SendBuffer
    send_buffer_.initialize(config_.queue_config, config_.shaping_config);
    std::cout << "  SendBuffer initialized" << std::endl;
    
    // 2. 创建 5 个 Sender
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        auto sender = createSender(priority);
        if (!sender) {
            std::cerr << "[UnifiedSender] Failed to create sender for priority " << i << std::endl;
            return false;
        }
        senders_.push_back(sender);
    }
    std::cout << "  5 Senders created" << std::endl;
    
    // 3. 初始化 Scheduler
    scheduler_.initialize(send_buffer_, senders_);
    std::cout << "  Scheduler initialized" << std::endl;
    
    // 4. 初始化 FeedbackController
    feedback_controller_.setRedundancyCallback(
        [this](DataPriority priority, float redundancy) {
            int idx = static_cast<int>(priority);
            if (idx >= 0 && idx < 5) {
                if (manual_redundancy_[idx]) {
                    // 手动设置了冗余度，跳过自适应调整
                    return;
                }
                if (senders_[idx]) {
                    senders_[idx]->setRepairRatio(redundancy);
                    std::cout << "[UnifiedSender] FEC adjusted for priority " << idx
                              << ": " << (redundancy * 100) << "%" << std::endl;
                }
            }
        });
    
    feedback_controller_.setRateCallback(
        [this](DataPriority priority, uint32_t rate) {
            // 可以在这里调整 Token Bucket 速率
            // send_buffer_.setShapingRate(priority, rate / 1000.0);  // kbps -> pps (approximate)
        });
    std::cout << "  FeedbackController initialized" << std::endl;
    
    std::cout << "[UnifiedSender] Initialization complete" << std::endl;
    return true;
}

void UnifiedSender::start() {
    if (running_) return;
    
    std::cout << "[UnifiedSender] Starting..." << std::endl;
    
    running_ = true;
    
    // 启动所有 Sender
    for (auto& sender : senders_) {
        if (sender) {
            sender->start();
        }
    }
    std::cout << "  All Senders started" << std::endl;
    
    // 启动 Scheduler
    scheduler_.start();
    
    // 同步 Sender 初始冗余度到 FeedbackController
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        if (senders_[i]) {
            feedback_controller_.setInitialRedundancy(
                priority, senders_[i]->getRepairRatio());
        }
    }
    
    // 启动 FeedbackController
    feedback_controller_.start();
    
    // 启动块处理线程
    block_process_thread_ = std::thread(&UnifiedSender::processBlockLoop, this);
    
    std::cout << "[UnifiedSender] Started" << std::endl;
}

void UnifiedSender::stop() {
    if (!running_) return;
    
    std::cout << "[UnifiedSender] Stopping..." << std::endl;
    
    running_ = false;
    
    // 刷新 BlockPartition
    block_partition_.flush();
    
    // 等待块处理线程结束
    if (block_process_thread_.joinable()) {
        block_process_thread_.join();
    }
    
    // 停止 Scheduler
    scheduler_.stop();
    
    // 停止 FeedbackController
    feedback_controller_.stop();
    
    // 停止所有 Sender
    for (auto& sender : senders_) {
        if (sender) {
            sender->stop();
        }
    }
    
    std::cout << "[UnifiedSender] Stopped" << std::endl;
}

bool UnifiedSender::send(DataPriority priority, uint64_t stream_id,
                         std::shared_ptr<std::string> data) {
    if (!running_) {
        std::cerr << "[UnifiedSender] Not running!" << std::endl;
        return false;
    }

    if (!data || data->empty()) {
        return false;
    }

    // Bypass 模式：跳过 BlockPartition，直接推入 SendBuffer
    if (bypass_fec_) {
        uint64_t seq = seq_counter_++;
        SendTask task(priority, stream_id, data, seq);
        if (send_buffer_.push(task, true)) {
            total_frames_in_++;
            total_blocks_out_++;  // bypass 下一帧即一块
        }
        return true;
    }

    // 添加到 BlockPartition 进行分块/聚合
    uint64_t seq = seq_counter_++;
    bool ok = block_partition_.addFrame(priority, stream_id, seq, data);

    if (ok) {
        total_frames_in_++;
    }

    return ok;
}

bool UnifiedSender::send(DataPriority priority, uint64_t stream_id, 
                         const std::vector<uint8_t>& data) {
    auto data_str = std::make_shared<std::string>(data.begin(), data.end());
    return send(priority, stream_id, data_str);
}

void UnifiedSender::processBlockLoop() {
    std::cout << "[UnifiedSender] Block process thread started" << std::endl;
    
    while (running_) {
        SourceBlock block;
        
        // 尝试从 BlockPartition 获取块
        if (block_partition_.getNextBlock(block)) {
            // 转换为 SendTask
            auto data_ptr = std::make_shared<std::string>(
                reinterpret_cast<const char*>(block.data.data()), 
                block.data.size());
            
            SendTask task(block.priority, block.stream_id, data_ptr, block.start_seq);
            
            // 推入 SendBuffer
            if (send_buffer_.push(task, true)) {
                total_blocks_out_++;
            }
        } else {
            // 没有块，稍微等待
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    // 处理剩余的块
    SourceBlock block;
    while (block_partition_.getNextBlock(block)) {
        auto data_ptr = std::make_shared<std::string>(
            reinterpret_cast<const char*>(block.data.data()), 
            block.data.size());
        
        SendTask task(block.priority, block.stream_id, data_ptr, block.start_seq);
        send_buffer_.push(task, true);
        total_blocks_out_++;
    }
    
    std::cout << "[UnifiedSender] Block process thread stopped" << std::endl;
}

std::shared_ptr<Sender> UnifiedSender::createSender(DataPriority priority) {
    uint16_t port = getPort(priority);
    uint16_t symbol_size = 1024;  // 默认
    float repair_ratio = 0.1f;    // 默认
    
    switch (priority) {
        case DataPriority::FC_COMMAND:
            symbol_size = 512;
            repair_ratio = 0.5f;
            break;
        case DataPriority::VOICE:
            symbol_size = 256;
            repair_ratio = 0.05f;
            break;
        case DataPriority::VIDEO:
            symbol_size = 1024;
            repair_ratio = 0.4f;
            break;
        case DataPriority::POINT_CLOUD:
            symbol_size = 1024;
            repair_ratio = 0.1f;
            break;
        case DataPriority::GRID_MAP:
            symbol_size = 1024;
            repair_ratio = 0.2f;
            break;
    }
    
    auto sender = std::make_shared<Sender>(config_.target_ip, port, symbol_size);
    sender->setRepairRatio(repair_ratio);
    manual_redundancy_[static_cast<int>(priority)] = true;  // 标记为手动设置，关闭自适应FEC
    sender->setBypassFec(bypass_fec_);
    
    // 设置反馈回调
    sender->setFeedbackCallback([this](const FeedbackPacket& feedback) {
        this->onFeedbackReceived(feedback);
    });
    
    return sender;
}

uint16_t UnifiedSender::getPort(DataPriority priority) const {
    switch (priority) {
        case DataPriority::FC_COMMAND: return 9000;
        case DataPriority::VOICE: return 9004;
        case DataPriority::VIDEO: return 9001;
        case DataPriority::POINT_CLOUD: return 9002;
        case DataPriority::GRID_MAP: return 9003;
        default: return 9000;
    }
}

void UnifiedSender::setRedundancy(DataPriority priority, float ratio) {
    int idx = static_cast<int>(priority);
    if (idx >= 0 && idx < 5 && senders_[idx]) {
        senders_[idx]->setRepairRatio(ratio);
        manual_redundancy_[idx] = true;
    }
}

void UnifiedSender::setRateLimit(DataPriority priority, uint32_t kbps) {
    scheduler_.setBandwidthLimit(priority, kbps);
}

void UnifiedSender::setBypassFec(bool enable) {
    bypass_fec_ = enable;
    // 如果 Sender 已创建，同步更新
    for (auto& sender : senders_) {
        if (sender) {
            sender->setBypassFec(enable);
        }
    }
}

void UnifiedSender::setDropRate(float rate) {
    for (auto& sender : senders_) {
        if (sender) {
            sender->setDropRate(rate);
        }
    }
}

float UnifiedSender::getCurrentRedundancy(DataPriority priority) const {
    return feedback_controller_.getCurrentRedundancy(priority);
}

void UnifiedSender::setNextRepairRatio(DataPriority priority, uint64_t stream_id, float ratio) {
    int idx = static_cast<int>(priority);
    if (idx >= 0 && idx < 5 && senders_[idx]) {
        senders_[idx]->setNextRepairRatio(stream_id, ratio);
    }
}

void UnifiedSender::onFeedbackReceived(const FeedbackPacket& feedback) {
    feedback_controller_.onFeedbackReceived(feedback);
}

void UnifiedSender::printStatistics() const {
    std::cout << "\n========== UnifiedSender Statistics ==========" << std::endl;
    std::cout << "Total frames in: " << total_frames_in_.load() << std::endl;
    std::cout << "Total blocks out: " << total_blocks_out_.load() << std::endl;
    
    block_partition_.printStatistics();
    scheduler_.printStatistics();
    feedback_controller_.printStatistics();
    
    std::cout << "==============================================\n" << std::endl;
}

void UnifiedSender::SetLogFile(const std::string& path) {
    for (auto& sender : senders_) {
        if (sender) {
            sender->SetLogFile(path);
        }
    }
}
