#include "send_buffer.h"

#include <algorithm>
#include <iostream>
#include <thread>

// ========== TokenBucket 实现 ==========

TokenBucket::TokenBucket(double rate_tokens_per_sec, double bucket_size)
    : rate_(rate_tokens_per_sec)
    , max_tokens_(bucket_size)
    , tokens_(bucket_size)
    , last_refill_time_(std::chrono::steady_clock::now()) {}

void TokenBucket::refill() {
    auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(now - last_refill_time_).count();
    
    if (elapsed_sec > 0) {
        tokens_ = std::min(max_tokens_, tokens_ + rate_ * elapsed_sec);
        last_refill_time_ = now;
    }
}

bool TokenBucket::tryConsume(double tokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    refill();
    
    if (tokens_ >= tokens) {
        tokens_ -= tokens;
        return true;
    }
    return false;
}

void TokenBucket::consume(double tokens) {
    std::unique_lock<std::mutex> lock(mutex_);
    refill();
    
    while (tokens_ < tokens) {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        lock.lock();
        refill();
    }
    
    tokens_ -= tokens;
}

double TokenBucket::getAvailableTokens() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(now - last_refill_time_).count();
    return std::min(max_tokens_, tokens_ + rate_ * elapsed_sec);
}

void TokenBucket::setRate(double rate_tokens_per_sec) {
    std::lock_guard<std::mutex> lock(mutex_);
    refill();
    rate_ = rate_tokens_per_sec;
}

// ========== PriorityQueue 实现 ==========

PriorityQueue::PriorityQueue(DataPriority priority, size_t max_size)
    : priority_(priority), max_size_(max_size), dropped_count_(0), total_wait_time_ms_(0), processed_count_(0) {}

bool PriorityQueue::push(const SendTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (queue_.size() >= max_size_) {
        return false;
    }
    
    queue_.push(task);
    return true;
}

bool PriorityQueue::pop(SendTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (queue_.empty()) {
        return false;
    }
    
    task = queue_.front();
    queue_.pop();
    
    // 统计等待时间
    auto wait_time = std::chrono::steady_clock::now() - task.enqueue_time;
    auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(wait_time).count();
    
    std::lock_guard<std::mutex> stat_lock(stat_mutex_);
    total_wait_time_ms_ += wait_ms;
    processed_count_++;
    
    return true;
}

bool PriorityQueue::peek(SendTask& task) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (queue_.empty()) {
        return false;
    }
    
    task = queue_.front();
    return true;
}

size_t PriorityQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool PriorityQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

bool PriorityQueue::full() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() >= max_size_;
}

void PriorityQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
}

bool PriorityQueue::dropOldest() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (queue_.empty()) {
        return false;
    }
    
    queue_.pop();
    
    std::lock_guard<std::mutex> stat_lock(stat_mutex_);
    dropped_count_++;
    
    return true;
}

size_t PriorityQueue::dropHalf() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t to_drop = queue_.size() / 2;
    size_t dropped = 0;
    
    for (size_t i = 0; i < to_drop && !queue_.empty(); ++i) {
        queue_.pop();
        dropped++;
    }
    
    std::lock_guard<std::mutex> stat_lock(stat_mutex_);
    dropped_count_ += dropped;
    
    return dropped;
}

double PriorityQueue::getAverageWaitTimeMs() const {
    std::lock_guard<std::mutex> lock(stat_mutex_);
    
    if (processed_count_ == 0) {
        return 0.0;
    }
    
    return static_cast<double>(total_wait_time_ms_) / processed_count_;
}

// ========== SendBuffer 实现 ==========

SendBuffer::SendBuffer() : initialized_(false) {
    total_pushed_ = 0;
    total_popped_ = 0;
    total_dropped_ = 0;
    for (int i = 0; i < 5; ++i) {
        per_type_pushed_[i] = 0;
        per_type_dropped_[i] = 0;
    }
}

SendBuffer::~SendBuffer() {
    // 清理资源
}

void SendBuffer::initialize(const QueueConfig& qconfig, const ShapingConfig& sconfig) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 创建五种类型的队列
    queues_[0] = std::make_unique<PriorityQueue>(DataPriority::FC_COMMAND, qconfig.fc_command_max);
    queues_[1] = std::make_unique<PriorityQueue>(DataPriority::VOICE, qconfig.voice_max);
    queues_[2] = std::make_unique<PriorityQueue>(DataPriority::VIDEO, qconfig.video_max);
    queues_[3] = std::make_unique<PriorityQueue>(DataPriority::POINT_CLOUD, qconfig.point_cloud_max);
    queues_[4] = std::make_unique<PriorityQueue>(DataPriority::GRID_MAP, qconfig.grid_map_max);
    
    // 创建 Token Bucket（带宽预留模式：使用 bps 按比特限流）
    // 如果 bps 已配置则使用 bps，否则回退到 pps 保持兼容
    double fc_rate = (sconfig.fc_rate_bps > 0) ? sconfig.fc_rate_bps : sconfig.fc_rate_pps;
    double voice_rate = (sconfig.voice_rate_bps > 0) ? sconfig.voice_rate_bps : sconfig.voice_rate_pps;
    double video_rate = (sconfig.video_rate_bps > 0) ? sconfig.video_rate_bps : sconfig.video_rate_pps;
    double pc_rate = (sconfig.pc_rate_bps > 0) ? sconfig.pc_rate_bps : sconfig.pc_rate_pps;
    double grid_rate = (sconfig.grid_rate_bps > 0) ? sconfig.grid_rate_bps : sconfig.grid_rate_pps;
    
    buckets_[0] = std::make_unique<TokenBucket>(fc_rate, fc_rate * sconfig.burst_factor);
    buckets_[1] = std::make_unique<TokenBucket>(voice_rate, voice_rate * sconfig.burst_factor);
    buckets_[2] = std::make_unique<TokenBucket>(video_rate, video_rate * sconfig.burst_factor);
    buckets_[3] = std::make_unique<TokenBucket>(pc_rate, pc_rate * sconfig.burst_factor);
    buckets_[4] = std::make_unique<TokenBucket>(grid_rate, grid_rate * sconfig.burst_factor);
    
    initialized_ = true;
    
    std::cout << "[SendBuffer] Initialized with:" << std::endl;
    std::cout << "  FC queue: " << qconfig.fc_command_max << std::endl;
    std::cout << "  Voice queue: " << qconfig.voice_max << std::endl;
    std::cout << "  Video queue: " << qconfig.video_max << std::endl;
    std::cout << "  PointCloud queue: " << qconfig.point_cloud_max << std::endl;
    std::cout << "  GridMap queue: " << qconfig.grid_map_max << std::endl;
}

PriorityQueue* SendBuffer::getQueue(DataPriority priority) {
    int idx = static_cast<int>(priority);
    if (idx >= 0 && idx < 5) {
        return queues_[idx].get();
    }
    return nullptr;
}

const PriorityQueue* SendBuffer::getQueue(DataPriority priority) const {
    int idx = static_cast<int>(priority);
    if (idx >= 0 && idx < 5) {
        return queues_[idx].get();
    }
    return nullptr;
}

TokenBucket* SendBuffer::getBucket(DataPriority priority) {
    int idx = static_cast<int>(priority);
    if (idx >= 0 && idx < 5) {
        return buckets_[idx].get();
    }
    return nullptr;
}

bool SendBuffer::push(const SendTask& task, bool force) {
    if (!initialized_) {
        std::cerr << "[SendBuffer] Not initialized!" << std::endl;
        return false;
    }
    
    auto queue = getQueue(task.priority);
    if (!queue) {
        return false;
    }
    
    // 检查拥塞
    float usage = static_cast<float>(queue->size()) / queue->capacity();
    if (usage > 0.8f && congestion_callback_) {
        congestion_callback_(task.priority, queue->size());
    }
    
    bool pushed = queue->push(task);
    
    if (!pushed && force) {
        // 强制模式：丢弃最老的任务，然后重试
        queue->dropOldest();
        total_dropped_++;
        per_type_dropped_[static_cast<int>(task.priority)]++;
        
        pushed = queue->push(task);
    }
    
    if (pushed) {
        total_pushed_++;
        per_type_pushed_[static_cast<int>(task.priority)]++;
        cv_.notify_one();
    }
    
    return pushed;
}

bool SendBuffer::pop(SendTask& task) {
    if (!initialized_) {
        return false;
    }
    
    // 按优先级顺序检查（FC > GridMap > Video > Voice > PointCloud）
    int priority_order[] = {0, 4, 2, 1, 3};  // FC_COMMAND, GRID_MAP, VIDEO, VOICE, POINT_CLOUD
    for (int idx : priority_order) {
        auto priority = static_cast<DataPriority>(idx);
        auto queue = getQueue(priority);
        auto bucket = getBucket(priority);
        
        if (!queue || !bucket) {
            continue;
        }
        
        if (queue->empty()) {
            continue;
        }
        
        // 带宽预留：先 peek 队首包大小，按比特数消费令牌
        SendTask front_task;
        if (!queue->peek(front_task)) {
            continue;
        }
        double tokens_needed = front_task.data->size() * 8;  // 字节转比特
        if (!bucket->tryConsume(tokens_needed)) {
            continue;  // 令牌不足，尝试下一个优先级
        }
        
        // 弹出任务
        if (queue->pop(task)) {
            total_popped_++;
            return true;
        }
        
        // 弹出失败，退回令牌
        // Note: TokenBucket 不支持退回，这里只是逻辑完整性
    }
    
    return false;
}

bool SendBuffer::pop(DataPriority priority, SendTask& task) {
    if (!initialized_) {
        return false;
    }
    
    auto queue = getQueue(priority);
    auto bucket = getBucket(priority);
    
    if (!queue || !bucket) {
        return false;
    }
    
    if (queue->empty()) {
        return false;
    }
    
    // 带宽预留：先 peek 队首包大小，按比特数消费令牌
    SendTask front_task;
    if (!queue->peek(front_task)) {
        return false;
    }
    double tokens_needed = front_task.data->size() * 8;  // 字节转比特
    if (!bucket->tryConsume(tokens_needed)) {
        return false;  // 令牌不足
    }
    
    // 弹出任务
    if (queue->pop(task)) {
        total_popped_++;
        return true;
    }
    
    return false;
}

bool SendBuffer::popBlocking(SendTask& task, uint32_t timeout_ms) {
    if (!initialized_) {
        return false;
    }
    
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto predicate = [this]() {
        // 检查是否有数据且令牌足够（按队首包大小计算所需令牌）
        for (int i = 0; i < 5; ++i) {
            auto priority = static_cast<DataPriority>(i);
            auto queue = getQueue(priority);
            auto bucket = getBucket(priority);
            
            if (!queue || !bucket || queue->empty()) {
                continue;
            }
            
            SendTask front_task;
            if (!queue->peek(front_task)) {
                continue;
            }
            
            double tokens_needed = front_task.data->size() * 8;
            if (bucket->getAvailableTokens() >= tokens_needed) {
                return true;
            }
        }
        return false;
    };
    
    bool ready;
    if (timeout_ms == 0) {
        cv_.wait(lock, predicate);
        ready = true;
    } else {
        ready = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
    }
    
    lock.unlock();
    
    if (!ready) {
        return false;
    }
    
    return pop(task);
}

bool SendBuffer::isFull(DataPriority priority) const {
    auto queue = getQueue(priority);
    if (!queue) {
        return true;
    }
    return queue->full();
}

size_t SendBuffer::getQueueSize(DataPriority priority) const {
    auto queue = getQueue(priority);
    if (!queue) {
        return 0;
    }
    return queue->size();
}

size_t SendBuffer::getTotalSize() const {
    size_t total = 0;
    for (int i = 0; i < 5; ++i) {
        total += getQueueSize(static_cast<DataPriority>(i));
    }
    return total;
}

size_t SendBuffer::getTotalCapacity() const {
    size_t total = 0;
    for (int i = 0; i < 5; ++i) {
        auto queue = getQueue(static_cast<DataPriority>(i));
        if (queue) {
            total += queue->capacity();
        }
    }
    return total;
}

bool SendBuffer::hasData() const {
    for (int i = 0; i < 5; ++i) {
        auto queue = getQueue(static_cast<DataPriority>(i));
        if (queue && !queue->empty()) {
            return true;
        }
    }
    return false;
}

bool SendBuffer::canSend(DataPriority priority, double tokens) const {
    auto bucket = const_cast<SendBuffer*>(this)->getBucket(priority);
    if (!bucket) {
        return false;
    }
    return bucket->getAvailableTokens() >= tokens;
}

bool SendBuffer::tryConsumeToken(DataPriority priority, double tokens) {
    auto bucket = getBucket(priority);
    if (!bucket) {
        return false;
    }
    return bucket->tryConsume(tokens);
}

size_t SendBuffer::dropLowPriorityOnCongestion(float threshold) {
    size_t total_dropped = 0;
    
    // 从低优先级到高优先级检查
    // 优先级：PointCloud(3) < GridMap(4) < Video(2) < Voice(1) < FC(0)
    // 丢弃顺序：PointCloud > GridMap > Video > Voice (FC 不丢弃)
    int drop_order[] = {3, 4, 2, 1};
    
    for (int idx : drop_order) {
        auto priority = static_cast<DataPriority>(idx);
        auto queue = getQueue(priority);
        
        if (!queue) {
            continue;
        }
        
        float usage = static_cast<float>(queue->size()) / queue->capacity();
        
        if (usage > threshold) {
            // 队列使用率超过阈值，丢弃一半
            size_t dropped = queue->dropHalf();
            total_dropped += dropped;
            per_type_dropped_[idx] += dropped;
            total_dropped_ += dropped;
            
            std::cout << "[SendBuffer] Dropped " << dropped << " tasks from priority " 
                      << idx << " (usage: " << (usage * 100) << "%)" << std::endl;
        }
    }
    
    return total_dropped;
}

SendBuffer::Statistics SendBuffer::getStatistics() const {
    Statistics stats;
    stats.total_pushed = total_pushed_.load();
    stats.total_popped = total_popped_.load();
    stats.total_dropped = total_dropped_.load();
    
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        auto queue = getQueue(priority);
        
        if (queue) {
            stats.current_size[i] = queue->size();
            stats.max_size[i] = queue->capacity();
        }
        
        uint64_t pushed = per_type_pushed_[i].load();
        uint64_t dropped = per_type_dropped_[i].load();
        
        if (pushed > 0) {
            stats.drop_rate[i] = static_cast<double>(dropped) / (pushed + dropped);
        }
    }
    
    return stats;
}

void SendBuffer::resetStatistics() {
    total_pushed_ = 0;
    total_popped_ = 0;
    total_dropped_ = 0;
    
    for (int i = 0; i < 5; ++i) {
        per_type_pushed_[i] = 0;
        per_type_dropped_[i] = 0;
    }
}

void SendBuffer::setShapingRate(DataPriority priority, double rate_pps) {
    auto bucket = getBucket(priority);
    if (bucket) {
        bucket->setRate(rate_pps);
    }
}

void SendBuffer::setCongestionCallback(CongestionCallback callback) {
    congestion_callback_ = callback;
}
