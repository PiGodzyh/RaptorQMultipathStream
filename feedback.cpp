#include "feedback.h"

#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>

// ========== StreamStats 实现 ==========

float StreamStats::getLossRate() const {
    uint32_t expected = expected_symbols.load();
    uint32_t received = received_symbols.load();
    
    if (expected == 0) return 0.0f;
    
    uint32_t lost = (expected > received) ? (expected - received) : 0;
    return static_cast<float>(lost) / expected;
}

void StreamStats::resetSnapshot() {
    snapshot_received_ = received_symbols.load();
    snapshot_expected_ = expected_symbols.load();
}

float StreamStats::getPhaseLossRate() const {
    uint32_t expected = expected_symbols.load();
    uint32_t received = received_symbols.load();
    
    if (expected == 0) return 0.0f;
    
    uint32_t phase_expected = expected - snapshot_expected_;
    uint32_t phase_received = received - snapshot_received_;
    
    // 如果阶段性样本太少，回退到累计统计
    if (phase_expected < 10) {
        uint32_t lost = (expected > received) ? (expected - received) : 0;
        return static_cast<float>(lost) / expected;
    }
    
    uint32_t phase_lost = (phase_expected > phase_received) 
                          ? (phase_expected - phase_received) 
                          : 0;
    return static_cast<float>(phase_lost) / phase_expected;
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
    float success_rate = 1.0f - loss_rate;
    float new_redundancy = current_redundancy;
    
    // 计算与目标成功率的偏差
    float deviation = success_rate - config_.target_success_rate;
    float tolerance = config_.success_tolerance;
    
    if (deviation < -tolerance) {
        // 成功率低于目标区间下限（< 80%），需要增加冗余
        float severity = std::min(1.0f, std::abs(deviation) / tolerance);
        new_redundancy += config_.increase_step * (1.0f + severity);
    } else if (deviation < 0.0f) {
        // 成功率在目标区间内但偏低（80%~90%），适度增加
        new_redundancy += config_.increase_step * 0.5f;
    } else if (deviation > tolerance * 0.5f) {
        // 成功率高于目标区间中上（> 95%），可以降低冗余
        new_redundancy -= config_.decrease_step;
    }
    // 否则：成功率在舒适区（90%~95%），维持当前冗余度
    
    // 基于延迟的微调（延迟高增加冗余，延迟低且成功率足够则降低）
    if (feedback.avg_delay_ms > config_.high_rtt_threshold_ms) {
        new_redundancy += config_.increase_step * 0.5f;
    } else if (feedback.avg_delay_ms < config_.low_rtt_threshold_ms && 
               deviation > tolerance * 0.5f) {
        new_redundancy -= config_.decrease_step * 0.5f;
    }
    
    // 动态下限：根据当前丢包率计算最低可行冗余度
    // 公式：repair_ratio >= loss_rate / (1 - loss_rate)
    // 保证在丢包率 X% 的网络中，冗余度不会低到无法解码
    float dynamic_min = loss_rate / (1.0f - loss_rate);
    dynamic_min = std::max(config_.min_redundancy, dynamic_min);
    dynamic_min = std::min(config_.max_redundancy, dynamic_min);
    
    // 限制在有效范围内（动态下限优先于固定下限）
    new_redundancy = std::max(dynamic_min, 
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
    constexpr uint32_t kMinRateKbps = 100;  // 速率下限 100kbps
    
    // 如果拥塞，降低速率
    if (isCongested(feedback)) {
        uint32_t new_rate = static_cast<uint32_t>(current_rate_kbps * 0.8f);
        return std::max(kMinRateKbps, new_rate);
    }
    
    // 如果网络状况良好（成功率高于目标上限），可以逐步恢复速率
    float success_rate = 1.0f - feedback.loss_rate;
    if (success_rate > config_.target_success_rate + config_.success_tolerance &&
        feedback.avg_delay_ms < config_.low_rtt_threshold_ms) {
        uint32_t new_rate = static_cast<uint32_t>(current_rate_kbps * 1.05f);
        return new_rate;
    }
    
    return current_rate_kbps;
}

bool AdaptiveFEC::isCongested(const FeedbackPacket& feedback) const {
    // 拥塞判断：成功率远低于目标，或高延迟
    float success_rate = 1.0f - feedback.loss_rate;
    return (success_rate < config_.target_success_rate - config_.success_tolerance) ||
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
        
        // 按优先级聚合反馈，每个优先级只调整一次冗余度
        std::map<DataPriority, std::pair<uint32_t, uint32_t>> agg_stats;  // priority -> (received, total)
        std::map<DataPriority, FeedbackPacket> agg_feedback;
        for (const auto& feedback : batch) {
            auto priority = feedback.priority;
            agg_stats[priority].first += feedback.received_symbols;
            agg_stats[priority].second += feedback.total_symbols;
            // 保留最后一个 feedback 的其他字段（延迟等）
            agg_feedback[priority] = feedback;
            feedback_received_++;
        }
        
        // 对每个优先级应用一次调整
        for (auto& pair : agg_stats) {
            auto priority = pair.first;
            uint32_t total_received = pair.second.first;
            uint32_t total_symbols = pair.second.second;
            
            if (total_symbols == 0) continue;
            
            // 构建聚合反馈包
            FeedbackPacket aggregated = agg_feedback[priority];
            aggregated.received_symbols = total_received;
            aggregated.total_symbols = total_symbols;
            aggregated.lost_symbols = (total_symbols > total_received) 
                                      ? (total_symbols - total_received) 
                                      : 0;
            aggregated.loss_rate = static_cast<float>(aggregated.lost_symbols) / total_symbols;
            
            float current_redundancy = getCurrentRedundancy(priority);
            uint32_t current_rate = getCurrentRate(priority);
            
            // 计算新的配置
            float new_redundancy = adaptive_fec_.calculateRedundancy(
                current_redundancy, aggregated);
            uint32_t new_rate = adaptive_fec_.calculateRate(
                current_rate, aggregated);
            
            // 应用调整
            if (new_redundancy != current_redundancy || new_rate != current_rate) {
                applyAdjustment(priority, new_redundancy, new_rate);
                adjustments_applied_++;
            }
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

void FeedbackController::setInitialRedundancy(DataPriority priority, float redundancy) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    current_redundancy_[priority] = redundancy;
    std::cout << "[FeedbackController] Initial redundancy synced for priority "
              << static_cast<int>(priority) << ": " << (redundancy * 100) << "%" << std::endl;
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
            
            // 发送反馈后重置阶段性统计快照
            pair.second->resetSnapshot();
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
        packet.loss_rate = stats->getPhaseLossRate();
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

// ========== FeedbackPacket 序列化 ==========

std::vector<uint8_t> FeedbackPacket::serialize() const {
    std::vector<uint8_t> data(kSerializedSize);
    size_t offset = 0;
    
    auto write_u64 = [&](uint64_t val) {
        data[offset++] = (val >> 56) & 0xFF;
        data[offset++] = (val >> 48) & 0xFF;
        data[offset++] = (val >> 40) & 0xFF;
        data[offset++] = (val >> 32) & 0xFF;
        data[offset++] = (val >> 24) & 0xFF;
        data[offset++] = (val >> 16) & 0xFF;
        data[offset++] = (val >> 8) & 0xFF;
        data[offset++] = val & 0xFF;
    };
    auto write_u32 = [&](uint32_t val) {
        data[offset++] = (val >> 24) & 0xFF;
        data[offset++] = (val >> 16) & 0xFF;
        data[offset++] = (val >> 8) & 0xFF;
        data[offset++] = val & 0xFF;
    };
    auto write_u8 = [&](uint8_t val) {
        data[offset++] = val;
    };
    auto write_float = [&](float val) {
        static_assert(sizeof(float) == 4, "float must be 4 bytes");
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(float));
        write_u32(bits);
    };
    
    write_u64(timestamp_ms);
    write_u8(static_cast<uint8_t>(priority));
    write_u32(stream_id);
    write_u32(received_symbols);
    write_u32(lost_symbols);
    write_u32(total_symbols);
    write_float(loss_rate);
    write_u32(avg_delay_ms);
    write_u32(jitter_ms);
    write_u32(rtt_ms);
    write_float(suggested_redundancy);
    write_u32(suggested_rate_kbps);
    
    return data;
}

bool FeedbackPacket::deserialize(const uint8_t* data, size_t len, FeedbackPacket& packet) {
    if (len != kSerializedSize) return false;
    
    size_t offset = 0;
    
    auto read_u64 = [&]() -> uint64_t {
        uint64_t val = 0;
        val |= static_cast<uint64_t>(data[offset++]) << 56;
        val |= static_cast<uint64_t>(data[offset++]) << 48;
        val |= static_cast<uint64_t>(data[offset++]) << 40;
        val |= static_cast<uint64_t>(data[offset++]) << 32;
        val |= static_cast<uint64_t>(data[offset++]) << 24;
        val |= static_cast<uint64_t>(data[offset++]) << 16;
        val |= static_cast<uint64_t>(data[offset++]) << 8;
        val |= static_cast<uint64_t>(data[offset++]);
        return val;
    };
    auto read_u32 = [&]() -> uint32_t {
        uint32_t val = 0;
        val |= static_cast<uint32_t>(data[offset++]) << 24;
        val |= static_cast<uint32_t>(data[offset++]) << 16;
        val |= static_cast<uint32_t>(data[offset++]) << 8;
        val |= static_cast<uint32_t>(data[offset++]);
        return val;
    };
    auto read_u8 = [&]() -> uint8_t {
        return data[offset++];
    };
    auto read_float = [&]() -> float {
        uint32_t bits = read_u32();
        float val;
        std::memcpy(&val, &bits, sizeof(float));
        return val;
    };
    
    packet.timestamp_ms = read_u64();
    packet.priority = static_cast<DataPriority>(read_u8());
    packet.stream_id = read_u32();
    packet.received_symbols = read_u32();
    packet.lost_symbols = read_u32();
    packet.total_symbols = read_u32();
    packet.loss_rate = read_float();
    packet.avg_delay_ms = read_u32();
    packet.jitter_ms = read_u32();
    packet.rtt_ms = read_u32();
    packet.suggested_redundancy = read_float();
    packet.suggested_rate_kbps = read_u32();
    
    return true;
}
