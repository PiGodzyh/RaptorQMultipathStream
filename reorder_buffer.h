#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <map>
#include <queue>
#include <functional>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <thread>

#include "send_buffer.h"  // 复用 DataPriority 定义

/**
 * ReorderBuffer - 接收端统一排序器
 * 
 * 功能：
 * 1. 统一处理所有数据类型的乱序到达
 * 2. 按 (stream_id, seq) 排序
 * 3. 超时等待机制（不同类型不同超时）
 * 4. 过期数据丢弃
 * 
 * 工作流程：
 * Receiver(RaptorQ解码) → ReorderBuffer(排序) → 应用层(有序数据)
 */

/**
 * 接收数据单元
 */
struct ReorderUnit {
    DataPriority priority;      // 数据优先级（决定超时时间）
    uint64_t stream_id;         // 流ID
    uint64_t seq;               // 序列号（用于排序）
    std::vector<uint8_t> data;  // 数据内容
    std::chrono::steady_clock::time_point receive_time;  // 接收时间
    
    ReorderUnit() : priority(DataPriority::FC_COMMAND), stream_id(0), seq(0) {}
    
    ReorderUnit(DataPriority prio, uint64_t sid, uint64_t s, std::vector<uint8_t>&& d)
        : priority(prio), stream_id(sid), seq(s), data(std::move(d)) {
        receive_time = std::chrono::steady_clock::now();
    }
    
    // 用于优先级队列比较（seq小的优先级高）
    bool operator>(const ReorderUnit& other) const {
        if (stream_id != other.stream_id) {
            return stream_id > other.stream_id;
        }
        return seq > other.seq;
    }
};

/**
 * 排序缓冲区配置
 */
struct ReorderConfig {
    // 超时配置（毫秒）
    uint32_t fc_timeout_ms = 30;        // 飞控：30ms
    uint32_t voice_timeout_ms = 50;     // 语音：50ms
    uint32_t video_timeout_ms = 100;    // 视频：100ms
    uint32_t pc_timeout_ms = 500;       // 点云：500ms
    uint32_t grid_timeout_ms = 200;     // 栅格：200ms
    
    // 缓冲区配置
    size_t max_buffer_size = 1000;      // 最大缓冲单元数
    size_t max_stream_buffer = 100;     // 单流最大缓冲数
    
    // 清理配置
    uint32_t cleanup_interval_ms = 100; // 清理周期
    
    ReorderConfig() {}
};

/**
 * 单流排序缓冲区
 */
class StreamReorderBuffer {
public:
    explicit StreamReorderBuffer(uint64_t stream_id, DataPriority priority,
                                  size_t max_size, uint32_t timeout_ms);
    
    /**
     * 插入数据单元
     * @return true 成功，false 缓冲区满
     */
    bool insert(ReorderUnit&& unit);
    
    /**
     * 获取下一个可交付的数据（seq连续且未超时）
     * @return true 有可交付数据
     */
    bool popDeliverable(ReorderUnit& unit);
    
    /**
     * 检查并清理超时数据
     * @return 清理的数量
     */
    size_t cleanupExpired();
    
    /**
     * 设置期望的下一个seq
     */
    void setNextExpectedSeq(uint64_t seq) { next_expected_seq_ = seq; }
    uint64_t getNextExpectedSeq() const { return next_expected_seq_; }
    
    /**
     * 获取缓冲区大小
     */
    size_t size() const;
    bool empty() const;
    
    /**
     * 获取流ID和优先级
     */
    uint64_t getStreamId() const { return stream_id_; }
    DataPriority getPriority() const { return priority_; }
    
    /**
     * 获取统计
     */
    uint64_t getReceivedCount() const { return received_count_; }
    uint64_t getDeliveredCount() const { return delivered_count_; }
    uint64_t getDroppedCount() const { return dropped_count_; }

private:
    uint64_t stream_id_;
    DataPriority priority_;
    size_t max_size_;
    uint32_t timeout_ms_;
    
    // 按seq排序的缓冲区（小根堆）
    std::priority_queue<ReorderUnit, std::vector<ReorderUnit>, std::greater<ReorderUnit>> buffer_;
    mutable std::mutex mutex_;
    
    // 下一个期望的seq
    uint64_t next_expected_seq_ = 0;
    
    // 统计
    uint64_t received_count_ = 0;
    uint64_t delivered_count_ = 0;
    uint64_t dropped_count_ = 0;
    
    // 检查是否超时
    bool isExpired(const ReorderUnit& unit) const;
};

/**
 * 统一排序缓冲区
 */
class ReorderBuffer {
public:
    explicit ReorderBuffer(const ReorderConfig& config = ReorderConfig());
    ~ReorderBuffer();
    
    /**
     * 启动清理线程
     */
    void start();
    
    /**
     * 停止清理线程
     */
    void stop();
    
    /**
     * 插入解码后的数据
     * @param priority 数据优先级
     * @param stream_id 流ID
     * @param seq 序列号
     * @param data 数据内容
     * @return true 成功
     */
    bool insert(DataPriority priority, uint64_t stream_id, 
                uint64_t seq, std::vector<uint8_t>&& data);
    
    /**
     * 插入（简化接口）
     */
    bool insert(const ReorderUnit& unit);
    
    /**
     * 取出下一个可交付的数据（按优先级和顺序）
     * @param unit 输出数据
     * @return true 成功
     */
    bool pop(ReorderUnit& unit);
    
    /**
     * 阻塞式取出
     */
    bool popBlocking(ReorderUnit& unit, uint32_t timeout_ms = 0);
    
    /**
     * 设置流的初始seq（用于新流）
     */
    void setStreamInitialSeq(uint64_t stream_id, DataPriority priority, uint64_t initial_seq);
    
    /**
     * 关闭流（清理该流的所有缓冲数据）
     */
    void closeStream(uint64_t stream_id);
    
    /**
     * 获取缓冲区统计
     */
    struct Statistics {
        uint64_t total_received = 0;
        uint64_t total_delivered = 0;
        uint64_t total_dropped = 0;
        uint64_t total_expired = 0;
        size_t current_streams = 0;
        size_t current_buffer_size = 0;
    };
    Statistics getStatistics() const;
    
    /**
     * 打印统计
     */
    void printStatistics() const;
    
    /**
     * 获取某流的缓冲区大小
     */
    size_t getStreamBufferSize(uint64_t stream_id) const;
    
    /**
     * 检查是否有可交付数据
     */
    bool hasDeliverable() const;

private:
    /**
     * 清理线程主循环
     */
    void cleanupLoop();
    
    /**
     * 获取或创建流缓冲区
     */
    StreamReorderBuffer* getOrCreateStreamBuffer(uint64_t stream_id, 
                                                  DataPriority priority);
    
    /**
     * 获取流的超时时间
     */
    uint32_t getTimeoutMs(DataPriority priority) const;
    
    /**
     * 选择下一个要交付的流（按优先级）
     */
    uint64_t selectNextStream();

private:
    ReorderConfig config_;
    
    // 流缓冲区映射
    std::map<uint64_t, std::unique_ptr<StreamReorderBuffer>> stream_buffers_;
    mutable std::mutex buffers_mutex_;
    
    // 流优先级记录（用于新流创建）
    std::map<uint64_t, DataPriority> stream_priorities_;
    
    // 清理线程
    std::thread cleanup_thread_;
    std::atomic<bool> running_{false};
    
    // 条件变量（用于阻塞式pop）
    std::condition_variable cv_;
    mutable std::mutex cv_mutex_;
    
    // 统计
    std::atomic<uint64_t> total_received_{0};
    std::atomic<uint64_t> total_delivered_{0};
    std::atomic<uint64_t> total_dropped_{0};
    std::atomic<uint64_t> total_expired_{0};
};

/**
 * ReorderBuffer 接收回调接口
 */
class ReorderBufferReceiver {
public:
    virtual ~ReorderBufferReceiver() = default;
    
    /**
     * 数据就绪回调（有序交付）
     */
    virtual void OnDataReady(DataPriority priority, uint64_t stream_id, 
                             uint64_t seq, const std::vector<uint8_t>& data) = 0;
    
    /**
     * 数据超时回调
     */
    virtual void OnDataExpired(DataPriority priority, uint64_t stream_id, 
                               uint64_t seq) {}
};
