#include "scheduler.h"

#include <iostream>
#include <algorithm>
#include <iomanip>

// ========== TransmissionScheduler 实现 ==========

TransmissionScheduler::TransmissionScheduler(const SchedulerConfig& config)
    : config_(config) {
    // 初始化带宽计数器
    for (int i = 0; i < 5; ++i) {
        bandwidth_counters_[i].window_start = std::chrono::steady_clock::now();
    }
}

TransmissionScheduler::~TransmissionScheduler() {
    stop();
}

void TransmissionScheduler::initialize(SendBuffer& buffer,
                                       std::vector<std::shared_ptr<Sender>> senders) {
    buffer_ = &buffer;
    senders_ = std::move(senders);
    
    if (senders_.size() != 5) {
        std::cerr << "[Scheduler] Warning: Expected 5 senders, got " << senders_.size() << std::endl;
    }
    
    std::cout << "[Scheduler] Initialized with algorithm: ";
    switch (config_.algorithm) {
        case ScheduleAlgorithm::STRICT_PRIORITY:
            std::cout << "Strict Priority";
            break;
        case ScheduleAlgorithm::WEIGHTED_ROUND_ROBIN:
            std::cout << "Weighted Round Robin";
            break;
        case ScheduleAlgorithm::BANDWIDTH_RATIO:
            std::cout << "Bandwidth Ratio";
            break;
        case ScheduleAlgorithm::ADAPTIVE:
            std::cout << "Adaptive";
            break;
    }
    std::cout << std::endl;
    std::cout << "  Total bandwidth: " << config_.total_bandwidth_kbps << " kbps" << std::endl;
}

void TransmissionScheduler::start() {
    if (running_) {
        return;
    }
    
    if (!buffer_) {
        std::cerr << "[Scheduler] Not initialized!" << std::endl;
        return;
    }
    
    running_ = true;
    scheduler_thread_ = std::thread(&TransmissionScheduler::scheduleLoop, this);
    
    std::cout << "[Scheduler] Started" << std::endl;
}

void TransmissionScheduler::stop() {
    running_ = false;
    
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
    
    std::cout << "[Scheduler] Stopped" << std::endl;
}

void TransmissionScheduler::scheduleLoop() {
    while (running_) {
        // 执行一次调度
        size_t scheduled = scheduleOnce();
        
        // 检查拥塞
        checkAndHandleCongestion();
        
        // 更新带宽统计
        updateBandwidthStats();
        
        // 如果没有任务，稍微等待
        if (scheduled == 0) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(config_.schedule_interval_us));
        }
    }
}

size_t TransmissionScheduler::scheduleOnce() {
    switch (config_.algorithm) {
        case ScheduleAlgorithm::STRICT_PRIORITY:
            return scheduleStrictPriority();
        case ScheduleAlgorithm::WEIGHTED_ROUND_ROBIN:
            return scheduleWeightedRoundRobin();
        case ScheduleAlgorithm::BANDWIDTH_RATIO:
            return scheduleBandwidthRatio();
        case ScheduleAlgorithm::ADAPTIVE:
            return scheduleAdaptive();
        default:
            return scheduleWeightedRoundRobin();
    }
}

size_t TransmissionScheduler::scheduleStrictPriority() {
    size_t scheduled = 0;
    SendTask task;
    
    // 先检查总带宽限制
    if (isTotalBandwidthExceeded()) {
        return scheduled;
    }
    
    // 按优先级顺序（FC=0 最高，PointCloud=3 最低）
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        
        // 检查带宽限制
        uint32_t bandwidth_limit = 0;
        switch (priority) {
            case DataPriority::FC_COMMAND: bandwidth_limit = config_.fc_bandwidth_kbps; break;
            case DataPriority::VOICE: bandwidth_limit = config_.voice_bandwidth_kbps; break;
            case DataPriority::VIDEO: bandwidth_limit = config_.video_bandwidth_kbps; break;
            case DataPriority::GRID_MAP: bandwidth_limit = config_.grid_map_bandwidth_kbps; break;
            case DataPriority::POINT_CLOUD: bandwidth_limit = config_.point_cloud_bandwidth_kbps; break;
        }
        
        if (bandwidth_limit > 0 && stats_.current_bandwidth_kbps[i] >= bandwidth_limit) {
            continue;  // 带宽已满
        }
        
        // 尝试获取并发送任务
        while (buffer_->pop(task)) {
            if (sendTask(task)) {
                scheduled++;
                
                // 只发送一个任务就返回，让低优先级有机会
                return scheduled;
            }
        }
    }
    
    return scheduled;
}

size_t TransmissionScheduler::scheduleWeightedRoundRobin() {
    size_t scheduled = 0;
    SendTask task;
    
    // 先检查总带宽限制
    if (isTotalBandwidthExceeded()) {
        return scheduled;
    }
    
    // 找到下一个有权重且有数据的服务
    for (int attempts = 0; attempts < 5; ++attempts) {
        // 选择下一个优先级
        last_served_ = (last_served_ + 1) % 5;
        auto priority = static_cast<DataPriority>(last_served_);
        int idx = last_served_;
        
        // 获取权重
        uint32_t weight = getWeight(priority);
        
        // 检查当前剩余权重
        if (current_weight_[idx] == 0) {
            current_weight_[idx] = weight;
        }
        
        // 检查带宽限制
        uint32_t bandwidth_limit = 0;
        switch (priority) {
            case DataPriority::FC_COMMAND: bandwidth_limit = config_.fc_bandwidth_kbps; break;
            case DataPriority::VOICE: bandwidth_limit = config_.voice_bandwidth_kbps; break;
            case DataPriority::VIDEO: bandwidth_limit = config_.video_bandwidth_kbps; break;
            case DataPriority::GRID_MAP: bandwidth_limit = config_.grid_map_bandwidth_kbps; break;
            case DataPriority::POINT_CLOUD: bandwidth_limit = config_.point_cloud_bandwidth_kbps; break;
        }
        
        if (bandwidth_limit > 0 && stats_.current_bandwidth_kbps[idx] >= bandwidth_limit) {
            current_weight_[idx] = 0;  // 带宽满，重置权重
            continue;
        }
        
        // 尝试获取并发送任务
        if (buffer_->pop(task)) {
            if (sendTask(task)) {
                scheduled++;
                current_weight_[idx]--;
                return scheduled;
            }
        } else {
            current_weight_[idx] = 0;  // 无数据，重置权重
        }
    }
    
    return scheduled;
}

size_t TransmissionScheduler::scheduleBandwidthRatio() {
    // 先检查总带宽限制
    if (isTotalBandwidthExceeded()) {
        return 0;
    }
    
    // 计算总带宽使用比例，优先发送使用率低于配额的数据类型
    double min_ratio = 2.0;
    int selected = -1;
    
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        
        uint32_t bandwidth_limit = 0;
        switch (priority) {
            case DataPriority::FC_COMMAND: bandwidth_limit = config_.fc_bandwidth_kbps; break;
            case DataPriority::VOICE: bandwidth_limit = config_.voice_bandwidth_kbps; break;
            case DataPriority::VIDEO: bandwidth_limit = config_.video_bandwidth_kbps; break;
            case DataPriority::GRID_MAP: bandwidth_limit = config_.grid_map_bandwidth_kbps; break;
            case DataPriority::POINT_CLOUD: bandwidth_limit = config_.point_cloud_bandwidth_kbps; break;
        }
        
        if (bandwidth_limit == 0) continue;
        
        double ratio = stats_.current_bandwidth_kbps[i] / bandwidth_limit;
        if (ratio < min_ratio) {
            min_ratio = ratio;
            selected = i;
        }
    }
    
    if (selected >= 0) {
        SendTask task;
        if (buffer_->pop(task)) {
            if (sendTask(task)) {
                return 1;
            }
        }
    }
    
    return 0;
}

size_t TransmissionScheduler::scheduleAdaptive() {
    // 自适应调度：根据拥塞情况动态切换策略
    float max_usage = 0.0f;
    for (int i = 0; i < 5; ++i) {
        max_usage = std::max(max_usage, 
            static_cast<float>(buffer_->getQueueSize(static_cast<DataPriority>(i))) / 
            buffer_->getTotalCapacity());
    }
    
    if (max_usage > config_.congestion_threshold) {
        // 拥塞时使用严格优先级，确保关键数据
        return scheduleStrictPriority();
    } else {
        // 正常时使用加权轮询，公平分配
        return scheduleWeightedRoundRobin();
    }
}

bool TransmissionScheduler::sendTask(const SendTask& task) {
    int idx = static_cast<int>(task.priority);
    
    if (idx < 0 || idx >= static_cast<int>(senders_.size()) || !senders_[idx]) {
        std::cerr << "[Scheduler] No sender for priority " << idx << std::endl;
        return false;
    }
    
    // 发送数据
    bool sent = senders_[idx]->sendData(task.stream_id, task.data);
    
    if (sent) {
        // 更新统计
        std::lock_guard<std::mutex> lock(stat_mutex_);
        stats_.scheduled_count[idx]++;
        stats_.total_scheduled++;
        stats_.total_bytes_sent[idx] += task.data->size();
        stats_.total_bytes += task.data->size();
        
        // 更新带宽计数器
        std::lock_guard<std::mutex> bw_lock(bandwidth_mutex_);
        bandwidth_counters_[idx].bytes_sent += task.data->size();
    } else {
        std::lock_guard<std::mutex> lock(stat_mutex_);
        stats_.dropped_count[idx]++;
        stats_.total_dropped++;
    }
    
    return sent;
}

void TransmissionScheduler::checkAndHandleCongestion() {
    // 获取队列使用率
    float max_usage = 0.0f;
    DataPriority congested_priority = DataPriority::FC_COMMAND;
    
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        size_t size = buffer_->getQueueSize(priority);
        size_t capacity = buffer_->getTotalCapacity() / 5;  // 近似
        
        float usage = static_cast<float>(size) / capacity;
        if (usage > max_usage) {
            max_usage = usage;
            congested_priority = priority;
        }
    }
    
    // 触发拥塞处理
    if (max_usage > config_.congestion_threshold) {
        if (congestion_callback_) {
            congestion_callback_(congested_priority, max_usage);
        }
        
        // 调用 SendBuffer 的拥塞处理
        buffer_->dropLowPriorityOnCongestion(config_.congestion_threshold);
    }
}

void TransmissionScheduler::updateBandwidthStats() {
    auto now = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(bandwidth_mutex_);
    
    for (int i = 0; i < 5; ++i) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - bandwidth_counters_[i].window_start).count();
        
        if (elapsed >= BANDWIDTH_WINDOW_MS) {
            // 计算带宽（kbps）
            double bytes_per_sec = bandwidth_counters_[i].bytes_sent * 1000.0 / elapsed;
            stats_.current_bandwidth_kbps[i] = bytes_per_sec * 8 / 1000;
            
            // 重置计数器
            bandwidth_counters_[i].bytes_sent = 0;
            bandwidth_counters_[i].window_start = now;
        }
    }
    
    // 更新队列大小统计
    for (int i = 0; i < 5; ++i) {
        stats_.queue_sizes[i] = static_cast<uint32_t>(
            buffer_->getQueueSize(static_cast<DataPriority>(i)));
    }
}

bool TransmissionScheduler::isTotalBandwidthExceeded() const {
    if (config_.total_bandwidth_kbps == 0) return false;
    
    std::lock_guard<std::mutex> lock(stat_mutex_);
    double total = 0;
    for (int i = 0; i < 5; ++i) {
        total += stats_.current_bandwidth_kbps[i];
    }
    return total >= config_.total_bandwidth_kbps;
}

uint32_t TransmissionScheduler::getWeight(DataPriority priority) const {
    switch (priority) {
        case DataPriority::FC_COMMAND: return config_.fc_weight;
        case DataPriority::VOICE: return config_.voice_weight;
        case DataPriority::VIDEO: return config_.video_weight;
        case DataPriority::GRID_MAP: return config_.grid_map_weight;
        case DataPriority::POINT_CLOUD: return config_.point_cloud_weight;
        default: return 50;
    }
}

void TransmissionScheduler::setAlgorithm(ScheduleAlgorithm algo) {
    config_.algorithm = algo;
    std::cout << "[Scheduler] Algorithm changed to " << static_cast<int>(algo) << std::endl;
}

void TransmissionScheduler::setWeights(const uint32_t weights[5]) {
    config_.fc_weight = weights[0];
    config_.voice_weight = weights[1];
    config_.video_weight = weights[2];
    config_.grid_map_weight = weights[3];
    config_.point_cloud_weight = weights[4];
}

void TransmissionScheduler::setBandwidthLimit(DataPriority priority, uint32_t kbps) {
    switch (priority) {
        case DataPriority::FC_COMMAND: config_.fc_bandwidth_kbps = kbps; break;
        case DataPriority::VOICE: config_.voice_bandwidth_kbps = kbps; break;
        case DataPriority::VIDEO: config_.video_bandwidth_kbps = kbps; break;
        case DataPriority::GRID_MAP: config_.grid_map_bandwidth_kbps = kbps; break;
        case DataPriority::POINT_CLOUD: config_.point_cloud_bandwidth_kbps = kbps; break;
    }
}

void TransmissionScheduler::setTotalBandwidthLimit(uint32_t kbps) {
    config_.total_bandwidth_kbps = kbps;
}

SchedulerStats TransmissionScheduler::getStatistics() const {
    std::lock_guard<std::mutex> lock(stat_mutex_);
    return stats_;
}

void TransmissionScheduler::resetStatistics() {
    std::lock_guard<std::mutex> lock(stat_mutex_);
    stats_ = SchedulerStats();
}

void TransmissionScheduler::printStatistics() const {
    auto stats = getStatistics();
    
    const char* names[] = {"FC", "Voice", "Video", "PointCloud", "GridMap"};
    
    std::cout << "\n========== Scheduler Statistics ==========" << std::endl;
    std::cout << "Total scheduled: " << stats.total_scheduled << std::endl;
    std::cout << "Total dropped: " << stats.total_dropped << std::endl;
    std::cout << "Total bytes: " << (stats.total_bytes / 1024 / 1024) << " MB" << std::endl;
    std::cout << std::endl;
    
    std::cout << std::left << std::setw(12) << "Type" 
              << std::setw(12) << "Scheduled"
              << std::setw(10) << "Dropped"
              << std::setw(15) << "Bandwidth"
              << std::setw(10) << "Queue" << std::endl;
    std::cout << std::string(59, '-') << std::endl;
    
    for (int i = 0; i < 5; ++i) {
        std::cout << std::left << std::setw(12) << names[i]
                  << std::setw(12) << stats.scheduled_count[i]
                  << std::setw(10) << stats.dropped_count[i]
                  << std::setw(14) << std::fixed << std::setprecision(1) 
                  << stats.current_bandwidth_kbps[i] << " "
                  << std::setw(9) << stats.queue_sizes[i] << std::endl;
    }
    std::cout << "==========================================\n" << std::endl;
}

void TransmissionScheduler::setCongestionCallback(CongestionCallback callback) {
    congestion_callback_ = callback;
}

// ========== SchedulerBuilder 实现 ==========

SchedulerBuilder& SchedulerBuilder::withAlgorithm(ScheduleAlgorithm algo) {
    config_.algorithm = algo;
    return *this;
}

SchedulerBuilder& SchedulerBuilder::withWeight(DataPriority priority, uint32_t weight) {
    switch (priority) {
        case DataPriority::FC_COMMAND: config_.fc_weight = weight; break;
        case DataPriority::VOICE: config_.voice_weight = weight; break;
        case DataPriority::VIDEO: config_.video_weight = weight; break;
        case DataPriority::GRID_MAP: config_.grid_map_weight = weight; break;
        case DataPriority::POINT_CLOUD: config_.point_cloud_weight = weight; break;
    }
    return *this;
}

SchedulerBuilder& SchedulerBuilder::withBandwidth(DataPriority priority, uint32_t kbps) {
    switch (priority) {
        case DataPriority::FC_COMMAND: config_.fc_bandwidth_kbps = kbps; break;
        case DataPriority::VOICE: config_.voice_bandwidth_kbps = kbps; break;
        case DataPriority::VIDEO: config_.video_bandwidth_kbps = kbps; break;
        case DataPriority::GRID_MAP: config_.grid_map_bandwidth_kbps = kbps; break;
        case DataPriority::POINT_CLOUD: config_.point_cloud_bandwidth_kbps = kbps; break;
    }
    return *this;
}

SchedulerBuilder& SchedulerBuilder::withTotalBandwidth(uint32_t kbps) {
    config_.total_bandwidth_kbps = kbps;
    return *this;
}

SchedulerBuilder& SchedulerBuilder::withCongestionThreshold(float threshold) {
    config_.congestion_threshold = threshold;
    return *this;
}

std::unique_ptr<TransmissionScheduler> SchedulerBuilder::create() const {
    return std::make_unique<TransmissionScheduler>(config_);
}
