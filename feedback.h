#pragma once

#include <cstdint>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <map>

#include "send_buffer.h"

/**
 * Feedback - 反馈机制与自适应 FEC 模块
 * 
 * 功能：
 * 1. 接收端周期性发送反馈包（丢包率/延迟/抖动）
 * 2. 发送端根据反馈动态调整 FEC 冗余度
 * 3. 拥塞时降低发送速率
 * 
 * 工作流程：
 * 接收端：收集统计 → 计算指标 → 发送反馈包
 * 发送端：接收反馈 → 自适应算法 → 调整 FEC/速率
 */

/**
 * 反馈包结构（接收端 → 发送端）
 */
struct FeedbackPacket {
    uint64_t timestamp_ms;          // 反馈包生成时间戳
    DataPriority priority;          // 数据类型
    uint32_t stream_id;             // 流ID
    
    // 接收统计
    uint32_t received_symbols;      // 收到的符号数
    uint32_t lost_symbols;          // 丢失的符号数
    uint32_t total_symbols;         // 应收到的符号总数
    
    // 计算指标
    float loss_rate;                // 丢包率 (0.0 - 1.0)
    uint32_t avg_delay_ms;          // 平均延迟
    uint32_t jitter_ms;             // 抖动
    uint32_t rtt_ms;                // 往返时延（估算）
    
    // 建议值（接收端计算）
    float suggested_redundancy;     // 建议冗余度
    uint32_t suggested_rate_kbps;   // 建议发送速率
    
    FeedbackPacket()
        : timestamp_ms(0)
        , priority(DataPriority::FC_COMMAND)
        , stream_id(0)
        , received_symbols(0)
        , lost_symbols(0)
        , total_symbols(0)
        , loss_rate(0.0f)
        , avg_delay_ms(0)
        , jitter_ms(0)
        , rtt_ms(0)
        , suggested_redundancy(0.1f)
        , suggested_rate_kbps(0) {}
    
    // 序列化到字节数组
    std::vector<uint8_t> serialize() const;
    
    // 反序列化
    static bool deserialize(const uint8_t* data, size_t len, FeedbackPacket& packet);
    
    // 序列化后的大小
    static constexpr size_t kSerializedSize = 49;  // 手动计算
};

/**
 * 流级统计信息（接收端收集）
 */
struct StreamStats {
    uint64_t stream_id;
    DataPriority priority;
    
    // 接收计数
    std::atomic<uint32_t> received_symbols{0};
    std::atomic<uint32_t> expected_symbols{0};
    
    // 阶段性统计快照（用于计算阶段性丢包率）
    uint32_t snapshot_received_ = 0;
    uint32_t snapshot_expected_ = 0;
    
    // 时间统计
    std::chrono::steady_clock::time_point first_receive_time;
    std::chrono::steady_clock::time_point last_receive_time;
    
    // 延迟样本（用于计算抖动）
    std::vector<uint32_t> delay_samples;  // 最近N个延迟样本
    mutable std::mutex delay_mutex;
    
    // 计算累计丢包率
    float getLossRate() const;
    
    // 计算阶段性丢包率（从上次 resetSnapshot 开始）
    float getPhaseLossRate() const;
    
    // 重置阶段性统计快照
    void resetSnapshot();
    
    // 计算平均延迟
    uint32_t getAvgDelayMs() const;
    
    // 计算抖动（延迟标准差）
    uint32_t getJitterMs() const;
    
    // 记录符号接收
    void onSymbolReceived(uint32_t symbol_id, uint32_t delay_ms);
};

/**
 * 自适应 FEC 算法
 */
class AdaptiveFEC {
public:
    struct Config {
        float min_redundancy;   // 最小冗余 5%
        float max_redundancy;   // 最大冗余 80%
        float increase_step;    // 每次增加 5%
        float decrease_step;    // 每次减少 2%
        
        // 目标成功率（核心参数）
        float target_success_rate;     // 目标成功率 90%
        float success_tolerance;       // 容忍区间 ±5%
        
        // 阈值（保留用于 RTT 判断）
        uint32_t high_rtt_threshold_ms;  // 高延迟阈值
        uint32_t low_rtt_threshold_ms;   // 低延迟阈值
        
        Config()
            : min_redundancy(0.40f)
            , max_redundancy(0.80f)
            , increase_step(0.03f)      // 每次增加 3%，更平缓
            , decrease_step(0.05f)      // 每次降低 5%，更积极
            , target_success_rate(0.90f)
            , success_tolerance(0.10f)  // 容忍区间 ±10%，舒适区 80%~100%
            , high_rtt_threshold_ms(200)
            , low_rtt_threshold_ms(50) {}
    };
    
    explicit AdaptiveFEC(const Config& config = Config());
    
    /**
     * 根据反馈计算新的 FEC 冗余度
     * @param current_redundancy 当前冗余度
     * @param feedback 反馈包
     * @return 新的冗余度
     */
    float calculateRedundancy(float current_redundancy, 
                              const FeedbackPacket& feedback);
    
    /**
     * 根据反馈计算新的发送速率
     * @param current_rate_kbps 当前速率
     * @param feedback 反馈包
     * @return 新的速率（kbps）
     */
    uint32_t calculateRate(uint32_t current_rate_kbps, 
                           const FeedbackPacket& feedback);
    
    /**
     * 获取当前拥塞状态
     */
    bool isCongested(const FeedbackPacket& feedback) const;

private:
    Config config_;
    
    // 历史记录（用于平滑调整）
    std::vector<float> loss_history_;
    mutable std::mutex history_mutex_;
};

/**
 * 反馈控制器（发送端使用）
 */
class FeedbackController {
public:
    using RedundancyCallback = std::function<void(DataPriority, float)>;
    using RateCallback = std::function<void(DataPriority, uint32_t)>;
    
    explicit FeedbackController(const AdaptiveFEC::Config& config = AdaptiveFEC::Config());
    ~FeedbackController();
    
    /**
     * 启动反馈处理线程
     */
    void start();
    
    /**
     * 停止
     */
    void stop();
    
    /**
     * 接收反馈包（从网络接收）
     */
    void onFeedbackReceived(const FeedbackPacket& feedback);
    
    /**
     * 注册冗余度调整回调
     */
    void setRedundancyCallback(RedundancyCallback callback);
    
    /**
     * 注册速率调整回调
     */
    void setRateCallback(RateCallback callback);
    
    /**
     * 获取当前 FEC 冗余度
     */
    float getCurrentRedundancy(DataPriority priority) const;
    
    /**
     * 获取当前建议速率
     */
    uint32_t getCurrentRate(DataPriority priority) const;
    
    /**
     * 打印统计
     */
    void printStatistics() const;
    
    /**
     * 设置初始冗余度（与实际 Sender 同步）
     */
    void setInitialRedundancy(DataPriority priority, float redundancy);

private:
    /**
     * 处理反馈的主循环
     */
    void processLoop();
    
    /**
     * 应用新的配置
     */
    void applyAdjustment(DataPriority priority, float redundancy, uint32_t rate);

private:
    AdaptiveFEC adaptive_fec_;
    
    // 当前配置（每种类型）
    std::map<DataPriority, float> current_redundancy_;
    std::map<DataPriority, uint32_t> current_rate_kbps_;
    mutable std::mutex config_mutex_;
    
    // 平滑后的丢包率（EWMA，减少随机波动）
    std::map<DataPriority, float> smoothed_loss_rate_;
    
    // 反馈队列
    std::vector<FeedbackPacket> feedback_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    
    // 回调
    RedundancyCallback redundancy_callback_;
    RateCallback rate_callback_;
    
    // 线程控制
    std::thread process_thread_;
    std::atomic<bool> running_{false};
    
    // 统计
    std::atomic<uint64_t> feedback_received_{0};
    std::atomic<uint64_t> adjustments_applied_{0};
};

/**
 * 反馈发送器（接收端使用）
 */
class FeedbackSender {
public:
    struct Config {
        uint32_t feedback_interval_ms;    // 反馈周期 100ms
        uint32_t report_interval_packets; // 每10个包报告一次
        uint32_t max_delay_samples;       // 最大延迟样本数
        
        Config()
            : feedback_interval_ms(500)   // 500ms 反馈周期，样本更稳定
            , report_interval_packets(10)
            , max_delay_samples(100) {}
    };
    
    explicit FeedbackSender(const Config& config = Config());
    ~FeedbackSender();
    
    /**
     * 启动反馈发送线程
     */
    void start();
    
    /**
     * 停止
     */
    void stop();
    
    /**
     * 设置发送回调（通过网络发送反馈包）
     */
    using SendCallback = std::function<void(const FeedbackPacket&)>;
    void setSendCallback(SendCallback callback);
    
    /**
     * 报告符号接收（Receiver 调用）
     * @param stream_id 流ID
     * @param priority 优先级
     * @param symbol_id 符号ID
     * @param send_timestamp_ms 发送时间戳（用于计算延迟）
     */
    void reportSymbolReceived(uint64_t stream_id, DataPriority priority,
                              uint32_t symbol_id, uint64_t send_timestamp_ms);
    
    /**
     * 报告符号丢失（Receiver 检测到丢包）
     */
    void reportSymbolLost(uint64_t stream_id, DataPriority priority,
                          uint32_t symbol_id);
    
    /**
     * 设置总符号数（用于计算丢包率）
     */
    void setExpectedSymbols(uint64_t stream_id, DataPriority priority,
                            uint32_t total_symbols);
    
    /**
     * 手动触发反馈发送
     */
    void sendFeedbackNow(uint64_t stream_id);
    
    /**
     * 获取统计
     */
    struct Statistics {
        uint64_t feedback_sent = 0;
        uint64_t symbols_reported = 0;
        uint64_t losses_reported = 0;
    };
    Statistics getStatistics() const;

private:
    /**
     * 发送线程主循环
     */
    void sendLoop();
    
    /**
     * 构建反馈包
     */
    FeedbackPacket buildFeedbackPacket(uint64_t stream_id);
    
    /**
     * 获取或创建流统计
     */
    StreamStats* getOrCreateStreamStats(uint64_t stream_id, DataPriority priority);

private:
    Config config_;
    
    // 流统计映射
    std::map<uint64_t, std::unique_ptr<StreamStats>> stream_stats_;
    std::mutex stats_mutex_;
    
    // 发送回调
    SendCallback send_callback_;
    
    // 发送线程
    std::thread send_thread_;
    std::atomic<bool> running_{false};
    
    // 统计
    std::atomic<uint64_t> feedback_sent_{0};
    std::atomic<uint64_t> symbols_reported_{0};
    std::atomic<uint64_t> losses_reported_{0};
};
