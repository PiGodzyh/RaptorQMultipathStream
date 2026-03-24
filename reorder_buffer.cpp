#include "reorder_buffer.h"

#include <iostream>
#include <algorithm>

// ========== StreamReorderBuffer 实现 ==========

StreamReorderBuffer::StreamReorderBuffer(uint64_t stream_id, DataPriority priority,
                                          size_t max_size, uint32_t timeout_ms)
    : stream_id_(stream_id)
    , priority_(priority)
    , max_size_(max_size)
    , timeout_ms_(timeout_ms)
    , next_expected_seq_(0)
    , received_count_(0)
    , delivered_count_(0)
    , dropped_count_(0) {}

bool StreamReorderBuffer::insert(ReorderUnit&& unit) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查是否已满
    if (buffer_.size() >= max_size_) {
        // 缓冲区满，丢弃最老的数据（seq最小的）
        if (!buffer_.empty()) {
            buffer_.pop();
            dropped_count_++;
        }
    }
    
    buffer_.push(std::move(unit));
    received_count_++;
    return true;
}

bool StreamReorderBuffer::popDeliverable(ReorderUnit& unit) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    while (!buffer_.empty()) {
        const ReorderUnit& top = buffer_.top();
        
        // 检查是否超时
        if (isExpired(top)) {
            // 超时，丢弃
            buffer_.pop();
            dropped_count_++;
            continue;
        }
        
        // 检查seq是否连续
        if (top.seq == next_expected_seq_) {
            // 可以交付
            unit = std::move(const_cast<ReorderUnit&>(top));
            buffer_.pop();
            next_expected_seq_++;
            delivered_count_++;
            return true;
        } else if (top.seq < next_expected_seq_) {
            // 已经过期的数据（重复或乱序延迟），丢弃
            buffer_.pop();
            dropped_count_++;
            continue;
        } else {
            // seq不连续，等待前面的数据
            return false;
        }
    }
    
    return false;
}

size_t StreamReorderBuffer::cleanupExpired() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t cleaned = 0;
    std::vector<ReorderUnit> temp;
    
    // 保留未超时的数据
    while (!buffer_.empty()) {
        ReorderUnit unit = std::move(const_cast<ReorderUnit&>(buffer_.top()));
        buffer_.pop();
        
        if (!isExpired(unit)) {
            temp.push_back(std::move(unit));
        } else {
            cleaned++;
            dropped_count_++;
        }
    }
    
    // 重新构建堆
    for (auto& unit : temp) {
        buffer_.push(std::move(unit));
    }
    
    return cleaned;
}

size_t StreamReorderBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

bool StreamReorderBuffer::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.empty();
}

bool StreamReorderBuffer::isExpired(const ReorderUnit& unit) const {
    auto elapsed = std::chrono::steady_clock::now() - unit.receive_time;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms_;
}

// ========== ReorderBuffer 实现 ==========

ReorderBuffer::ReorderBuffer(const ReorderConfig& config)
    : config_(config) {}

ReorderBuffer::~ReorderBuffer() {
    stop();
}

void ReorderBuffer::start() {
    if (running_) return;
    
    running_ = true;
    cleanup_thread_ = std::thread(&ReorderBuffer::cleanupLoop, this);
    
    std::cout << "[ReorderBuffer] Started" << std::endl;
}

void ReorderBuffer::stop() {
    running_ = false;
    cv_.notify_all();
    
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    std::cout << "[ReorderBuffer] Stopped" << std::endl;
}

void ReorderBuffer::cleanupLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.cleanup_interval_ms));
        
        if (!running_) break;
        
        // 清理所有流的过期数据
        std::lock_guard<std::mutex> lock(buffers_mutex_);
        size_t total_expired = 0;
        
        for (auto& pair : stream_buffers_) {
            total_expired += pair.second->cleanupExpired();
        }
        
        if (total_expired > 0) {
            total_expired_ += total_expired;
            std::cout << "[ReorderBuffer] Cleaned up " << total_expired << " expired units" << std::endl;
        }
    }
}

StreamReorderBuffer* ReorderBuffer::getOrCreateStreamBuffer(uint64_t stream_id, 
                                                             DataPriority priority) {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    
    auto it = stream_buffers_.find(stream_id);
    if (it != stream_buffers_.end()) {
        return it->second.get();
    }
    
    // 创建新的流缓冲区
    uint32_t timeout_ms = getTimeoutMs(priority);
    auto buffer = std::make_unique<StreamReorderBuffer>(
        stream_id, priority, config_.max_stream_buffer, timeout_ms);
    
    StreamReorderBuffer* ptr = buffer.get();
    stream_buffers_[stream_id] = std::move(buffer);
    stream_priorities_[stream_id] = priority;
    
    std::cout << "[ReorderBuffer] Created stream buffer: id=" << stream_id 
              << ", priority=" << static_cast<int>(priority)
              << ", timeout=" << timeout_ms << "ms" << std::endl;
    
    return ptr;
}

uint32_t ReorderBuffer::getTimeoutMs(DataPriority priority) const {
    switch (priority) {
        case DataPriority::FC_COMMAND: return config_.fc_timeout_ms;
        case DataPriority::VOICE: return config_.voice_timeout_ms;
        case DataPriority::VIDEO: return config_.video_timeout_ms;
        case DataPriority::POINT_CLOUD: return config_.pc_timeout_ms;
        case DataPriority::GRID_MAP: return config_.grid_timeout_ms;
        default: return 100;
    }
}

bool ReorderBuffer::insert(DataPriority priority, uint64_t stream_id, 
                           uint64_t seq, std::vector<uint8_t>&& data) {
    ReorderUnit unit(priority, stream_id, seq, std::move(data));
    return insert(unit);
}

bool ReorderBuffer::insert(const ReorderUnit& unit) {
    StreamReorderBuffer* stream_buffer = getOrCreateStreamBuffer(unit.stream_id, unit.priority);
    
    if (!stream_buffer) {
        return false;
    }
    
    bool success = stream_buffer->insert(ReorderUnit(unit));
    if (success) {
        total_received_++;
        cv_.notify_one();
    }
    
    return success;
}

bool ReorderBuffer::pop(ReorderUnit& unit) {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    
    // 按优先级选择流（FC > Voice > Video > PC > Grid）
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        
        // 找到该优先级的流
        for (auto& pair : stream_buffers_) {
            if (pair.second->getPriority() == priority) {
                if (pair.second->popDeliverable(unit)) {
                    total_delivered_++;
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool ReorderBuffer::popBlocking(ReorderUnit& unit, uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(cv_mutex_);
    
    auto predicate = [this]() {
        ReorderUnit temp;
        return this->pop(temp);
    };
    
    bool ready;
    if (timeout_ms == 0) {
        cv_.wait(lock, predicate);
        ready = true;
    } else {
        ready = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
    }
    
    lock.unlock();
    
    if (ready) {
        return pop(unit);
    }
    return false;
}

void ReorderBuffer::setStreamInitialSeq(uint64_t stream_id, DataPriority priority, 
                                        uint64_t initial_seq) {
    StreamReorderBuffer* buffer = getOrCreateStreamBuffer(stream_id, priority);
    if (buffer) {
        buffer->setNextExpectedSeq(initial_seq);
    }
}

void ReorderBuffer::closeStream(uint64_t stream_id) {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    
    auto it = stream_buffers_.find(stream_id);
    if (it != stream_buffers_.end()) {
        // 统计剩余未交付的数据
        size_t remaining = it->second->size();
        if (remaining > 0) {
            std::cout << "[ReorderBuffer] Closing stream " << stream_id 
                      << ", dropping " << remaining << " undelivered units" << std::endl;
            total_dropped_ += remaining;
        }
        
        stream_buffers_.erase(it);
        stream_priorities_.erase(stream_id);
    }
}

ReorderBuffer::Statistics ReorderBuffer::getStatistics() const {
    Statistics stats;
    stats.total_received = total_received_.load();
    stats.total_delivered = total_delivered_.load();
    stats.total_dropped = total_dropped_.load();
    stats.total_expired = total_expired_.load();
    
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    stats.current_streams = stream_buffers_.size();
    
    for (const auto& pair : stream_buffers_) {
        stats.current_buffer_size += pair.second->size();
    }
    
    return stats;
}

void ReorderBuffer::printStatistics() const {
    auto stats = getStatistics();
    
    std::cout << "\n========== ReorderBuffer Statistics ==========" << std::endl;
    std::cout << "Total received: " << stats.total_received << std::endl;
    std::cout << "Total delivered: " << stats.total_delivered << std::endl;
    std::cout << "Total dropped: " << stats.total_dropped << std::endl;
    std::cout << "Total expired: " << stats.total_expired << std::endl;
    std::cout << "Active streams: " << stats.current_streams << std::endl;
    std::cout << "Buffered units: " << stats.current_buffer_size << std::endl;
    std::cout << "==============================================\n" << std::endl;
}

size_t ReorderBuffer::getStreamBufferSize(uint64_t stream_id) const {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    
    auto it = stream_buffers_.find(stream_id);
    if (it != stream_buffers_.end()) {
        return it->second->size();
    }
    return 0;
}

bool ReorderBuffer::hasDeliverable() const {
    std::lock_guard<std::mutex> lock(buffers_mutex_);
    
    for (const auto& pair : stream_buffers_) {
        ReorderUnit unit;
        // 尝试获取但不实际取出（需要非const，这里简化处理）
        if (!pair.second->empty()) {
            return true;
        }
    }
    
    return false;
}
