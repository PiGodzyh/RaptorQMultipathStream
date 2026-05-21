#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>
#include <atomic>
#include <chrono>
#include <functional>
#include <condition_variable>

#include "stream_config.h"

/**
 * SendBuffer - 发送端缓存与流量控制模块
 * 
 * 功能：
 * 1. 为每种数据类型提供独立的发送缓存队列
 * 2. 队列长度限制和溢出丢弃策略
 * 3. Token Bucket 流量整形
 * 4. 优先级仲裁（配合调度器使用）
 */

/**
 * 五种数据类型枚举（与系统架构一致）
 */
enum class DataPriority : uint8_t {
    FC_COMMAND = 0,     // 飞控指令 - 最高优先级
    VOICE = 1,          // 语音
    VIDEO = 2,          // 视频
    POINT_CLOUD = 3,    // 点云
    GRID_MAP = 4,       // 栅格地图
    COUNT = 5           // 类型数量
};

/**
 * 发送任务结构
 */
struct SendTask {
    DataPriority priority;              // 优先级/数据类型
    uint64_t stream_id;                 // 流ID
    std::shared_ptr<std::string> data;  // 数据内容
    uint64_t seq;                       // 序列号（用于排序）
    std::chrono::steady_clock::time_point enqueue_time;  // 入队时间
    
    SendTask() : priority(DataPriority::FC_COMMAND), stream_id(0), seq(0) {}
    
    SendTask(DataPriority prio, uint64_t sid, std::shared_ptr<std::string> d, uint64_t s)
        : priority(prio), stream_id(sid), data(std::move(d)), seq(s) {
        enqueue_time = std::chrono::steady_clock::now();
    }
};

/**
 * Token Bucket 流量整形器
 */
class TokenBucket {
public:
    /**
     * 构造函数
     * @param rate_tokens_per_sec 令牌产生速率（令牌/秒）
     * @param bucket_size 桶容量（最大突发令牌数）
     */
    TokenBucket(double rate_tokens_per_sec, double bucket_size);
    
    /**
     * 尝试获取指定数量的令牌
     * @param tokens 需要的令牌数
     * @return true 成功获取，false 令牌不足
     */
    bool tryConsume(double tokens);
    
    /**
     * 阻塞式获取令牌（等待直到获取成功）
     * @param tokens 需要的令牌数
     */
    void consume(double tokens);
    
    /**
     * 获取当前令牌数
     */
    double getAvailableTokens() const;
    
    /**
     * 更新速率
     */
    void setRate(double rate_tokens_per_sec);

private:
    void refill();
    
    double rate_;           // 令牌产生速率（令牌/秒）
    double max_tokens_;     // 桶容量
    double tokens_;         // 当前令牌数
    std::chrono::steady_clock::time_point last_refill_time_;
    mutable std::mutex mutex_;
};

/**
 * 单类型发送队列
 */
class PriorityQueue {
public:
    explicit PriorityQueue(DataPriority priority, size_t max_size = 100);
    
    /**
     * 推入任务
     * @return true 成功，false 队列满
     */
    bool push(const SendTask& task);
    
    /**
     * 弹出任务
     * @return true 成功，false 队列空
     */
    bool pop(SendTask& task);
    
    /**
     * 查看队首任务（不弹出）
     * @return true 成功，false 队列空
     */
    bool peek(SendTask& task) const;
    
    /**
     * 获取当前队列大小
     */
    size_t size() const;
    
    /**
     * 检查队列是否为空
     */
    bool empty() const;
    
    /**
     * 检查队列是否已满
     */
    bool full() const;
    
    /**
     * 清空队列
     */
    void clear();
    
    /**
     * 丢弃最老的任务（队列满时）
     * @return true 成功丢弃，false 队列为空
     */
    bool dropOldest();
    
    /**
     * 丢弃最老的一半任务（紧急腾空间）
     * @return 丢弃的任务数
     */
    size_t dropHalf();
    
    /**
     * 获取队列最大容量
     */
    size_t capacity() const { return max_size_; }
    
    /**
     * 获取平均等待时间（毫秒）
     */
    double getAverageWaitTimeMs() const;

private:
    DataPriority priority_;
    size_t max_size_;
    std::queue<SendTask> queue_;
    mutable std::mutex mutex_;
    
    // 统计
    mutable std::mutex stat_mutex_;
    uint64_t dropped_count_ = 0;
    uint64_t total_wait_time_ms_ = 0;
    uint64_t processed_count_ = 0;
};

/**
 * 发送缓存管理器
 */
class SendBuffer {
public:
    /**
     * 队列配置（每种数据类型的最大队列长度）
     */
    struct QueueConfig {
        size_t fc_command_max;      // 飞控：小队列，快速响应
        size_t voice_max;           // 语音：中等队列
        size_t video_max;           // 视频：较大队列
        size_t point_cloud_max;     // 点云：最大队列（实时性低，可大量缓冲）
        size_t grid_map_max;        // 栅格：大队列（可容忍延迟）
        
        QueueConfig()
            : fc_command_max(10)
            , voice_max(50)
            , video_max(100)
            , point_cloud_max(500)   // 实时性低，增大缓冲
            , grid_map_max(300)      // 可容忍延迟，增大缓冲
        {}
    };
    
    /**
     * 流量整形配置
     */
    struct ShapingConfig {
        double fc_rate_pps;     // 飞控：1000 pkt/s（兼容保留）
        double voice_rate_pps;  // 语音：500 pkt/s（兼容保留）
        double video_rate_pps;  // 视频：10000 pkt/s（兼容保留）
        double pc_rate_pps;     // 点云：2000 pkt/s（兼容保留）
        double grid_rate_pps;   // 栅格：3000 pkt/s（兼容保留）
        double burst_factor;    // 突发因子（桶容量 = 速率 * 因子）
        
        // 带宽预留：比特/秒（bps），由 Scheduler 配额同步
        double fc_rate_bps;
        double voice_rate_bps;
        double video_rate_bps;
        double pc_rate_bps;
        double grid_rate_bps;
        
        ShapingConfig()
            : fc_rate_pps(1000.0)
            , voice_rate_pps(500.0)
            , video_rate_pps(10000.0)
            , pc_rate_pps(2000.0)
            , grid_rate_pps(3000.0)
            , burst_factor(0.05)   // 50ms 数据量，限制突发
            , fc_rate_bps(0)
            , voice_rate_bps(0)
            , video_rate_bps(0)
            , pc_rate_bps(0)
            , grid_rate_bps(0) {}
    };

public:
    SendBuffer();
    ~SendBuffer();
    
    /**
     * 初始化（配置队列大小和流量整形）
     */
    void initialize(const QueueConfig& qconfig = QueueConfig(),
                    const ShapingConfig& sconfig = ShapingConfig());
    
    /**
     * 推入发送任务
     * @param task 发送任务
     * @param force 是否强制推入（队列满时丢弃最老的任务）
     * @return true 成功，false 队列满且非强制模式
     */
    bool push(const SendTask& task, bool force = false);
    
    /**
     * 弹出最高优先级且令牌足够的任务
     * @param task 输出任务
     * @return true 成功，false 所有队列为空或令牌不足
     */
    bool pop(SendTask& task);
    
    /**
     * 阻塞式弹出（等待直到有任务可用且令牌足够）
     * @param task 输出任务
     * @param timeout_ms 超时时间（毫秒，0表示无限等待）
     * @return true 成功，false 超时
     */
    bool popBlocking(SendTask& task, uint32_t timeout_ms = 0);
    
    /**
     * 检查指定类型队列是否已满
     */
    bool isFull(DataPriority priority) const;
    
    /**
     * 获取指定类型队列的当前大小
     */
    size_t getQueueSize(DataPriority priority) const;
    
    /**
     * 获取所有队列的总大小
     */
    size_t getTotalSize() const;
    
    /**
     * 获取总容量
     */
    size_t getTotalCapacity() const;
    
    /**
     * 检查是否有任何队列非空
     */
    bool hasData() const;
    
    /**
     * 检查是否有足够令牌发送指定类型的数据
     */
    bool canSend(DataPriority priority, double tokens = 1.0) const;
    
    /**
     * 尝试消耗令牌（用于外部流量控制）
     */
    bool tryConsumeToken(DataPriority priority, double tokens = 1.0);
    
    /**
     * 拥塞时丢弃低优先级数据
     * @param threshold 触发阈值（队列使用率 > threshold 时触发）
     * @return 丢弃的任务数
     */
    size_t dropLowPriorityOnCongestion(float threshold = 0.8f);
    
    /**
     * 获取统计信息
     */
    struct Statistics {
        uint64_t total_pushed = 0;
        uint64_t total_popped = 0;
        uint64_t total_dropped = 0;
        size_t current_size[5] = {0};
        size_t max_size[5] = {0};
        double drop_rate[5] = {0};
    };
    Statistics getStatistics() const;
    
    /**
     * 重置统计
     */
    void resetStatistics();
    
    /**
     * 更新流量整形速率
     */
    void setShapingRate(DataPriority priority, double rate_pps);
    
    /**
     * 设置拥塞回调（队列满时通知）
     */
    using CongestionCallback = std::function<void(DataPriority, size_t)>;
    void setCongestionCallback(CongestionCallback callback);

private:
    PriorityQueue* getQueue(DataPriority priority);
    const PriorityQueue* getQueue(DataPriority priority) const;
    TokenBucket* getBucket(DataPriority priority);
    
    std::unique_ptr<PriorityQueue> queues_[5];
    std::unique_ptr<TokenBucket> buckets_[5];
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    
    // 统计
    std::atomic<uint64_t> total_pushed_{0};
    std::atomic<uint64_t> total_popped_{0};
    std::atomic<uint64_t> total_dropped_{0};
    std::atomic<uint64_t> per_type_pushed_[5];
    std::atomic<uint64_t> per_type_dropped_[5];
    
    CongestionCallback congestion_callback_;
    bool initialized_ = false;
};
