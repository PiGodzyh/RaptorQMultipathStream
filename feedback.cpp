#include "feedback.h"

#include <iostream>
#include <algorithm>
#include <cmath>

// ========== StreamStats 实现 ==========

float StreamStats::getLossRate() const {
    uint32_t expected = expected_symbols.load();
    uint32_t received = received_symbols.load();
    
    if (expected == 0) return 0.0f;
    
    uint32_t lost = (expected > received) ? (expected - received) : 0;
    return static_cast<float>(lost) / expected;
}

uint32_t StreamStats::getAvgDelayMs() const {
    std::lock_guard<std::mutex> lock(delay_mutex);
    
    if (delay_samples.empty()) return 0;
    
    uint64_t sum = 0;
    for (auto d : delay_samples) {
        sum += d;
    }
    return static_cast<uint32_t>(sum / delay_samples.size());
}

uint32_t StreamStats::getJitterMs() const {
    std::lock_guard<std::mutex> lock(delay_mutex);
    
    if (delay_samples.size() < 2) return 0;
    
    // 计算标准差
    double sum = 0;
    for (auto d : delay_samples) {
        sum += d;
    }
    double mean = sum / delay_samples.size();
    
    double variance = 0;
    for (auto d : delay_samples) {
        variance += (d - mean) * (d - mean);
    }
    variance /= delay_samples.size();
    
    return static_cast<uint32_t>(std::sqrt(variance));
}

void StreamStats::onSymbolReceived(uint32_t symbol_id, uint32_t delay_ms) {
    received_symbols++;
    
    auto now = std::chrono::steady_clock::now();
    if (first_receive_time == std::chrono::steady_clock::time_point()) {
        first_receive_time = now;
    }
    last_receive_time = now;
    
    // 记录延迟样本
    std::lock_guard<std::mutex> lock(delay_mutex);
    delay_samples.push_back(delay_ms);
    
    // 限制样本数量
    if (delay_samples.size() > 100) {
        delay_samples.erase(delay_samples.begin());
    }
}

// ========== AdaptiveFEC 实现 ==========

AdaptiveFEC::AdaptiveFEC(const Config& config) : config_(config) {}

float AdaptiveFEC::calculateRedundancy(float current_redundancy, 
                                       const FeedbackPacket& feedback) {
    float loss_rate = feedback.loss_rate;
    float new_redundancy = current_redundancy;
    
    // 基于丢包率调整
    if (loss_rate > config_.high_loss_threshold) {
        // 高丢包，大幅增加冗余
        new_redundancy += config_.increase_step * 2;
    } else if (loss_rate > config_.medium_loss_threshold) {
        // 中等丢包，适度增加冗余
        new_redundancy += config_.increase_step;
    } else if (loss_rate < config_.low_loss_threshold) {
        // 低丢包，可以降低冗余
        new_redundancy -= config_.decrease_step;
    }
    
    // 基于延迟调整（延迟高可能意味着拥塞）
    if (feedback.avg_delay_ms > config_.high_rtt_threshold_ms) {
        // 延迟高，增加冗余以应对潜在丢包
        new_redundancy += config_.increase_step;
    } else if (feedback.avg_delay_ms < config_.low_rtt_threshold_ms && 
               loss_rate < config_.low_loss_threshold) {
        // 延迟低且丢包少，可以降低冗余
        new_redundancy -= config_.decrease_step * 0.5f;
    }
    
    // 限制在有效范围内
    new_redundancy = std::max(config_.min_redundancy, 
                              std::min(config_.max_redundancy, new_redundancy));
    
    // 记录历史
    std::lock_guard<std::mutex> lock(history_mutex_);
    loss_history_.push_back(loss_rate);
    if (loss_history_.size() > 10) {
        loss_history_.erase(loss_history_.begin());
    }
    
    return new_redundancy;
}

uint32_t AdaptiveFEC::calculateRate(uint32_t current_rate_kbps,
                                    const FeedbackPacket& feedback) {
    // 如果拥塞，降低速率
    if (isCongested(feedback)) {
        return static_cast<uint32_t>(current_rate_kbps * 0.8f);  // 降低 20%
    }
    
    // 如果网络状况良好，可以逐步恢复速率
    if (feedback.loss_rate < config_.low_loss_threshold &&
        feedback.avg_delay_ms < config_.low_rtt_threshold_ms) {
        return static_cast<uint32_t>(current_rate_kbps * 1.05f);  // 增加 5%
    }
    
    return current_rate_kbps;
}

bool AdaptiveFEC::isCongested(const FeedbackPacket& feedback) const {
    // 拥塞判断：高丢包或高延迟
    return (feedback.loss_rate > config_.high_loss_threshold) ||
           (feedback.avg_delay_ms > config_.high_rtt_threshold_ms);
}

// ========== FeedbackController 实现 ==========

FeedbackController::FeedbackController(const AdaptiveFEC::Config& config)
    : adaptive_fec_(config) {
    // 初始化默认配置
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        current_redundancy_[priority] = 0.1f;  // 默认 10% 冗余
        current_rate_kbps_[priority] = 1000;   // 默认 1Mbps
    }
}

FeedbackController::~FeedbackController() {
    stop();
}

void FeedbackController::start() {
    if (running_) return;
    
    running_ = true;
    process_thread_ = std::thread(&FeedbackController::processLoop, this);
    
    std::cout << "[FeedbackController] Started" << std::endl;
}

void FeedbackController::stop() {
    running_ = false;
    cv_.notify_all();
    
    if (process_thread_.joinable()) {
        process_thread_.join();
    }
    
    std::cout << "[FeedbackController] Stopped" << std::endl;
}

void FeedbackController::processLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
            return !feedback_queue_.empty() || !running_;
        });
        
        if (!running_) break;
        
        // 复制队列并清空
        std::vector<FeedbackPacket> batch = std::move(feedback_queue_);
        feedback_queue_.clear();
        lock.unlock();
        
        // 处理反馈
        for (const auto& feedback : batch) {
            auto priority = feedback.priority;
            
            float current_redundancy = getCurrentRedundancy(priority);
            uint32_t current_rate = getCurrentRate(priority);
            
            // 计算新的配置
            float new_redundancy = adaptive_fec_.calculateRedundancy(
                current_redundancy, feedback);
            uint32_t new_rate = adaptive_fec_.calculateRate(
                current_rate, feedback);
            
            // 应用调整
            if (new_redundancy != current_redundancy || new_rate != current_rate) {
                applyAdjustment(priority, new_redundancy, new_rate);
                adjustments_applied_++;
            }
            
            feedback_received_++;
        }
    }
}

void FeedbackController::onFeedbackReceived(const FeedbackPacket& feedback) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    feedback_queue_.push_back(feedback);
    cv_.notify_one();
}

void FeedbackController::applyAdjustment(DataPriority priority, 
                                          float redundancy, 
                                          uint32_t rate) {
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        current_redundancy_[priority] = redundancy;
        current_rate_kbps_[priority] = rate;
    }
    
    // 触发回调
    if (redundancy_callback_) {
        redundancy_callback_(priority, redundancy);
    }
    if (rate_callback_) {
        rate_callback_(priority, rate);
    }
    
    const char* names[] = {"FC", "Voice", "Video", "PC", "Grid"};
    std::cout << "[FeedbackController] Adjusted " << names[static_cast<int>(priority)]
              << ": redundancy=" << (redundancy * 100) << "%"
              << ", rate=" << rate << "kbps" << std::endl;
}

void FeedbackController::setRedundancyCallback(RedundancyCallback callback) {
    redundancy_callback_ = callback;
}

void FeedbackController::setRateCallback(RateCallback callback) {
    rate_callback_ = callback;
}

float FeedbackController::getCurrentRedundancy(DataPriority priority) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    auto it = current_redundancy_.find(priority);
    if (it != current_redundancy_.end()) {
        return it->second;
    }
    return 0.1f;  // 默认值
}

uint32_t FeedbackController::getCurrentRate(DataPriority priority) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    auto it = current_rate_kbps_.find(priority);
    if (it != current_rate_kbps_.end()) {
        return it->second;
    }
    return 1000;  // 默认值
}

void FeedbackController::printStatistics() const {
    std::cout << "\n========== FeedbackController Statistics ==========" << std::endl;
    std::cout << "Feedback received: " << feedback_received_.load() << std::endl;
    std::cout << "Adjustments applied: " << adjustments_applied_.load() << std::endl;
    
    std::cout << "\nCurrent configuration:" << std::endl;
    const char* names[] = {"FC", "Voice", "Video", "PC", "Grid"};
    for (int i = 0; i < 5; ++i) {
        auto priority = static_cast<DataPriority>(i);
        std::cout << "  " << names[i] << ": redundancy=" 
                  << (getCurrentRedundancy(priority) * 100) << "%"
                  << ", rate=" << getCurrentRate(priority) << "kbps" << std::endl;
    }
    std::cout << "===================================================\n" << std::endl;
}

// ========== FeedbackSender 实现 ==========

FeedbackSender::FeedbackSender(const Config& config) : config_(config) {}

FeedbackSender::~FeedbackSender() {
    stop();
}

void FeedbackSender::start() {
    if (running_) return;
    
    running_ = true;
    send_thread_ = std::thread(&FeedbackSender::sendLoop, this);
    
    std::cout << "[FeedbackSender] Started (interval=" << config_.feedback_interval_ms << "ms)" << std::endl;
}

void FeedbackSender::stop() {
    running_ = false;
    
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
    
    std::cout << "[FeedbackSender] Stopped" << std::endl;
}

void FeedbackSender::sendLoop() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.feedback_interval_ms));
        
        if (!running_) break;
        
        // 为每个流发送反馈
        std::lock_guard<std::mutex> lock(stats_mutex_);
        for (const auto& pair : stream_stats_) {
            auto packet = buildFeedbackPacket(pair.first);
            
            if (send_callback_) {
                send_callback_(packet);
                feedback_sent_++;
            }
        }
    }
}

FeedbackPacket FeedbackSender::buildFeedbackPacket(uint64_t stream_id) {
    FeedbackPacket packet;
    packet.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    packet.stream_id = stream_id;
    
    auto stats = getOrCreateStreamStats(stream_id, DataPriority::VIDEO);
    if (stats) {
        packet.priority = stats->priority;
        packet.received_symbols = stats->received_symbols.load();
        packet.total_symbols = stats->expected_symbols.load();
        packet.lost_symbols = (packet.total_symbols > packet.received_symbols) 
                              ? (packet.total_symbols - packet.received_symbols) 
                              : 0;
        packet.loss_rate = stats->getLossRate();
        packet.avg_delay_ms = stats->getAvgDelayMs();
        packet.jitter_ms = stats->getJitterMs();
        
        // 计算建议值
        if (packet.loss_rate > 0.20f) {
            packet.suggested_redundancy = 0.50f;  // 高丢包建议50%冗余
        } else if (packet.loss_rate > 0.10f) {
            packet.suggested_redundancy = 0.30f;  // 中丢包建议30%冗余
        } else {
            packet.suggested_redundancy = 0.10f;  // 低丢包建议10%冗余
        }
    }
    
    return packet;
}

StreamStats* FeedbackSender::getOrCreateStreamStats(uint64_t stream_id, 
                                                     DataPriority priority) {
    auto it = stream_stats_.find(stream_id);
    if (it != stream_stats_.end()) {
        return it->second.get();
    }
    
    auto stats = std::make_unique<StreamStats>();
    stats->stream_id = stream_id;
    stats->priority = priority;
    
    StreamStats* ptr = stats.get();
    stream_stats_[stream_id] = std::move(stats);
    return ptr;
}

void FeedbackSender::setSendCallback(SendCallback callback) {
    send_callback_ = callback;
}

void FeedbackSender::reportSymbolReceived(uint64_t stream_id, 
                                          DataPriority priority,
                                          uint32_t symbol_id, 
                                          uint64_t send_timestamp_ms) {
    auto stats = getOrCreateStreamStats(stream_id, priority);
    if (!stats) return;
    
    // 计算延迟
    auto now = std::chrono::steady_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    uint32_t delay_ms = (now_ms > send_timestamp_ms) 
                        ? static_cast<uint32_t>(now_ms - send_timestamp_ms) 
                        : 0;
    
    stats->onSymbolReceived(symbol_id, delay_ms);
    symbols_reported_++;
}

void FeedbackSender::reportSymbolLost(uint64_t stream_id, 
                                      DataPriority priority,
                                      uint32_t symbol_id) {
    // 丢失统计可以通过 expected - received 计算
    // 这里仅做标记
    losses_reported_++;
}

void FeedbackSender::setExpectedSymbols(uint64_t stream_id, 
                                        DataPriority priority,
                                        uint32_t total_symbols) {
    auto stats = getOrCreateStreamStats(stream_id, priority);
    if (stats) {
        stats->expected_symbols.store(total_symbols);
    }
}

void FeedbackSender::sendFeedbackNow(uint64_t stream_id) {
    auto packet = buildFeedbackPacket(stream_id);
    if (send_callback_) {
        send_callback_(packet);
        feedback_sent_++;
    }
}

FeedbackSender::Statistics FeedbackSender::getStatistics() const {
    Statistics stats;
    stats.feedback_sent = feedback_sent_.load();
    stats.symbols_reported = symbols_reported_.load();
    stats.losses_reported = losses_reported_.load();
    return stats;
}
