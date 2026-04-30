#pragma once

#include <cstdint>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <string>

#include "send_buffer.h"
#include "sender.h"

/**
 * TransmissionScheduler - 发送端统一调度器
 * 
 * 功能：
 * 1. 管理五种数据类型的发送队列
 * 2. 多种调度算法（严格优先级/加权轮询/带宽比例）
 * 3. 总带宽限制和分配
 * 4. 与 SendBuffer 配合实现拥塞控制
 */

// 调度算法类型
enum class ScheduleAlgorithm {
    STRICT_PRIORITY,    // 严格优先级：FC > Voice > Video > Grid > PC
    WEIGHTED_ROUND_ROBIN, // 加权轮询
    BANDWIDTH_RATIO,    // 带宽比例分配
    ADAPTIVE            // 自适应（根据拥塞动态调整）
};

// 调度配置
struct SchedulerConfig {
    ScheduleAlgorithm algorithm = ScheduleAlgorithm::WEIGHTED_ROUND_ROBIN;
    
    // 权重配置（用于加权轮询）
    uint32_t fc_weight = 100;           // 飞控权重
    uint32_t voice_weight = 80;         // 语音权重
    uint32_t video_weight = 60;         // 视频权重
    uint32_t grid_map_weight = 70;      // 栅格权重
    uint32_t point_cloud_weight = 40;   // 点云权重
    
    // 带宽限制（kbps，0表示无限制）
    uint32_t total_bandwidth_kbps = 10000;  // 总带宽限制 10Mbps
    uint32_t fc_bandwidth_kbps = 100;       // 飞控 100kbps
    uint32_t voice_bandwidth_kbps = 500;    // 语音 500kbps
    uint32_t video_bandwidth_kbps = 6000;   // 视频 6Mbps
    uint32_t grid_map_bandwidth_kbps = 2000;// 栅格 2Mbps
    uint32_t point_cloud_bandwidth_kbps = 1500; // 点云 1.5Mbps
    
    // 队列阈值（触发拥塞处理）
    float congestion_threshold = 0.8f;
    
    // 调度间隔（微秒）
    uint32_t schedule_interval_us = 100;
    
    SchedulerConfig() {}
};

// 调度统计
struct SchedulerStats {
    uint64_t scheduled_count[5] = {0};      // 各类型调度次数
    uint64_t dropped_count[5] = {0};        // 各类型丢弃次数
    uint64_t total_bytes_sent[5] = {0};     // 各类型发送字节数
    double current_bandwidth_kbps[5] = {0}; // 当前带宽使用率
    uint32_t queue_sizes[5] = {0};          // 当前队列大小
    
    // 总体统计
    uint64_t total_scheduled = 0;
    uint64_t total_dropped = 0;
    uint64_t total_bytes = 0;
    double avg_latency_ms = 0;
};

/**
 * 发送调度器
 */
class TransmissionScheduler {
public:
    explicit TransmissionScheduler(const SchedulerConfig& config = SchedulerConfig());
    ~TransmissionScheduler();
    
    /**
     * 初始化调度器
     * @param buffer SendBuffer 引用（用于获取任务）
     * @param senders Sender 指针数组（每种类型一个 Sender）
     */
    void initialize(SendBuffer& buffer, 
                    std::vector<std::shared_ptr<Sender>> senders);
    
    /**
     * 启动调度器
     */
    void start();
    
    /**
     * 停止调度器
     */
    void stop();
    
    /**
     * 设置调度算法
     */
    void setAlgorithm(ScheduleAlgorithm algo);
    
    /**
     * 更新权重（加权轮询模式）
     */
    void setWeights(const uint32_t weights[5]);
    
    /**
     * 更新带宽限制
     */
    void setBandwidthLimit(DataPriority priority, uint32_t kbps);
    void setTotalBandwidthLimit(uint32_t kbps);
    
    /**
     * 检查是否正在运行
     */
    bool isRunning() const { return running_; }
    
    /**
     * 获取统计信息
     */
    SchedulerStats getStatistics() const;
    
    /**
     * 重置统计
     */
    void resetStatistics();
    
    /**
     * 打印统计信息
     */
    void printStatistics() const;
    
    /**
     * 手动触发一次调度（用于测试）
     * @return 调度的任务数
     */
    size_t scheduleOnce();
    
    /**
     * 设置拥塞回调
     */
    using CongestionCallback = std::function<void(DataPriority, float)>;
    void setCongestionCallback(CongestionCallback callback);

private:
    /**
     * 调度线程主循环
     */
    void scheduleLoop();
    
    /**
     * 严格优先级调度
     * @return 调度的任务数
     */
    size_t scheduleStrictPriority();
    
    /**
     * 加权轮询调度
     * @return 调度的任务数
     */
    size_t scheduleWeightedRoundRobin();
    
    /**
     * 带宽比例调度
     * @return 调度的任务数
     */
    size_t scheduleBandwidthRatio();
    
    /**
     * 自适应调度
     * @return 调度的任务数
     */
    size_t scheduleAdaptive();
    
    /**
     * 发送任务
     * @return true 成功
     */
    bool sendTask(const SendTask& task);
    
    /**
     * 检查拥塞并处理
     */
    void checkAndHandleCongestion();
    
    /**
     * 更新带宽统计
     */
    void updateBandwidthStats();
    
    /**
     * 获取权重
     */
    uint32_t getWeight(DataPriority priority) const;
    
    /**
     * 检查总带宽是否超限（基于 current_bandwidth_kbps 统计）
     */
    bool isTotalBandwidthExceeded() const;

private:
    SchedulerConfig config_;
    
    // 依赖组件
    SendBuffer* buffer_ = nullptr;
    std::vector<std::shared_ptr<Sender>> senders_;
    
    // 调度线程
    std::thread scheduler_thread_;
    std::atomic<bool> running_{false};
    
    // 加权轮询状态
    uint32_t current_weight_[5] = {0};  // 当前剩余权重
    int last_served_ = -1;              // 上次服务的优先级
    
    // 带宽统计
    struct BandwidthCounter {
        uint64_t bytes_sent = 0;
        std::chrono::steady_clock::time_point window_start;
    };
    BandwidthCounter bandwidth_counters_[5];
    mutable std::mutex bandwidth_mutex_;
    
    // 统计
    mutable std::mutex stat_mutex_;
    SchedulerStats stats_;
    
    // 拥塞回调
    CongestionCallback congestion_callback_;
    
    // 常量
    static constexpr size_t BANDWIDTH_WINDOW_MS = 1000;  // 带宽统计窗口 1秒
};

/**
 * 调度器构建器（简化配置）
 */
class SchedulerBuilder {
public:
    SchedulerBuilder& withAlgorithm(ScheduleAlgorithm algo);
    SchedulerBuilder& withWeight(DataPriority priority, uint32_t weight);
    SchedulerBuilder& withBandwidth(DataPriority priority, uint32_t kbps);
    SchedulerBuilder& withTotalBandwidth(uint32_t kbps);
    SchedulerBuilder& withCongestionThreshold(float threshold);
    
    SchedulerConfig build() const { return config_; }
    std::unique_ptr<TransmissionScheduler> create() const;

private:
    SchedulerConfig config_;
};
