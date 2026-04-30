#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <deque>
#include <functional>
#include <chrono>

#include "send_buffer.h"

/**
 * BlockPartition - 发送端动态分块模块
 * 
 * 功能：
 * 1. 大帧拆分（I帧 > 64KB 时拆分为多个连续 Source Block）
 * 2. 小帧聚合（多个 P 帧合并为一个 Source Block）
 * 3. 分块边界标记（Block ID 递增 + 连续标记）
 * 
 * 工作流程：
 * 原始数据帧 → BlockPartition（分块/聚合）→ Source Block → RaptorQ 编码 → 网络发送
 */

/**
 * 分块策略配置
 */
struct BlockPolicy {
    size_t max_block_size = 64 * 1024;    // 单块最大 64KB
    size_t min_block_size = 1 * 1024;     // 单块最小 1KB
    size_t target_block_size = 32 * 1024; // 目标块大小 32KB
    uint32_t max_aggregation = 5;         // 最大聚合帧数
    uint32_t aggregation_timeout_ms = 10; // 聚合超时 10ms
    bool enable_aggregation = false;      // 是否启用小帧聚合（默认关闭，低延迟场景用）
    
    BlockPolicy() {}
};

/**
 * 块类型
 */
enum class BlockType : uint8_t {
    SINGLE = 0,      // 单帧块（不拆分不聚合）
    SPLIT_START = 1, // 拆分开始块
    SPLIT_MIDDLE = 2,// 拆分中间块
    SPLIT_END = 3,   // 拆分结束块
    AGGREGATED = 4   // 聚合块（包含多帧）
};

/**
 * Source Block 结构
 */
struct SourceBlock {
    uint32_t block_id;              // 块ID（全局递增）
    BlockType type;                 // 块类型
    DataPriority priority;          // 数据优先级
    uint64_t stream_id;             // 流ID
    uint64_t start_seq;             // 起始序列号
    uint32_t num_frames;            // 包含的原始帧数
    
    // 数据
    std::vector<uint8_t> data;
    
    // 时间戳
    std::chrono::steady_clock::time_point create_time;
    
    // 边界标记
    bool is_first_block;            // 是否是第一个块
    bool is_last_block;             // 是否是最后一个块
    uint32_t total_blocks;          // 总分块数（拆分场景）
    uint32_t block_index;           // 当前块索引（拆分场景）
    
    SourceBlock()
        : block_id(0)
        , type(BlockType::SINGLE)
        , priority(DataPriority::FC_COMMAND)
        , stream_id(0)
        , start_seq(0)
        , num_frames(1)
        , is_first_block(true)
        , is_last_block(true)
        , total_blocks(1)
        , block_index(0) {
        create_time = std::chrono::steady_clock::now();
    }
};

/**
 * 帧信息（用于聚合）
 */
struct PendingFrame {
    uint64_t seq;
    std::shared_ptr<std::string> data;
    DataPriority priority;
    uint64_t stream_id;
    std::chrono::steady_clock::time_point enqueue_time;
    
    PendingFrame(uint64_t s, std::shared_ptr<std::string> d, 
                 DataPriority p, uint64_t sid)
        : seq(s), data(d), priority(p), stream_id(sid) {
        enqueue_time = std::chrono::steady_clock::now();
    }
};

/**
 * 动态分块器
 */
class BlockPartition {
public:
    explicit BlockPartition(const BlockPolicy& policy = BlockPolicy());
    ~BlockPartition() = default;
    
    /**
     * 添加一帧数据
     * @param priority 数据优先级
     * @param stream_id 流ID
     * @param seq 序列号
     * @param data 数据内容
     * @return true 成功
     */
    bool addFrame(DataPriority priority, uint64_t stream_id, 
                  uint64_t seq, std::shared_ptr<std::string> data);
    
    /**
     * 获取下一个 Source Block（非阻塞）
     * @param block 输出块
     * @return true 成功获取
     */
    bool getNextBlock(SourceBlock& block);
    
    /**
     * 检查是否有可用的块
     */
    bool hasBlock() const;
    
    /**
     * 获取当前队列中的块数量
     */
    size_t getQueueSize() const;
    
    /**
     * 获取当前聚合缓冲区中的帧数
     */
    size_t getPendingFramesCount() const;
    
    /**
     * 强制刷新聚合缓冲区（生成聚合块）
     */
    void flush();
    
    /**
     * 更新策略
     */
    void setPolicy(const BlockPolicy& policy);
    
    /**
     * 获取统计信息
     */
    struct Statistics {
        uint64_t total_frames_in = 0;       // 输入帧数
        uint64_t total_blocks_out = 0;      // 输出块数
        uint64_t split_frames = 0;          // 被拆分的帧数
        uint64_t aggregated_frames = 0;     // 被聚合的帧数
        uint64_t single_blocks = 0;         // 单帧块数
        uint64_t avg_block_size = 0;        // 平均块大小
        uint64_t max_block_size = 0;        // 最大块大小
    };
    Statistics getStatistics() const;
    
    /**
     * 打印统计
     */
    void printStatistics() const;

private:
    /**
     * 处理大帧拆分
     */
    void splitLargeFrame(const PendingFrame& frame);
    
    /**
     * 尝试聚合小帧
     */
    bool tryAggregateFrame(const PendingFrame& frame);
    
    /**
     * 创建聚合块
     */
    void createAggregatedBlock();
    
    /**
     * 创建单帧块
     */
    void createSingleBlock(const PendingFrame& frame);
    
    /**
     * 检查是否需要强制刷新（超时）
     */
    bool needFlush() const;
    
    /**
     * 获取下一个块ID
     */
    uint32_t getNextBlockId();

private:
    BlockPolicy policy_;
    
    // 聚合缓冲区
    std::deque<PendingFrame> pending_frames_;
    mutable std::mutex pending_mutex_;
    
    // 输出队列
    std::deque<SourceBlock> output_queue_;
    mutable std::mutex output_mutex_;
    
    // 块ID计数器
    std::atomic<uint32_t> block_id_counter_{0};
    
    // 统计
    Statistics stats_;
    mutable std::mutex stats_mutex_;
};

/**
 * BlockPartition 与 SendBuffer 的适配器
 * 将 SourceBlock 转换为 SendTask
 */
class BlockPartitionAdapter {
public:
    /**
     * 将 SourceBlock 转换为 SendTask
     */
    static SendTask toSendTask(const SourceBlock& block);
    
    /**
     * 从 SendTask 中提取 SourceBlock 信息（接收端使用）
     */
    static bool fromSendTask(const SendTask& task, SourceBlock& block);
};

/**
 * 便捷函数：创建配置了 BlockPartition 的发送流程
 * 
 * 使用示例：
 * BlockPartition partitioner;
 * SendBuffer buffer;
 * 
 * // 添加帧
 * partitioner.addFrame(VIDEO, stream_id, seq, data);
 * 
 * // 获取块并入队
 * SourceBlock block;
 * while (partitioner.getNextBlock(block)) {
 *     SendTask task = BlockPartitionAdapter::toSendTask(block);
 *     buffer.push(task, true);
 * }
 */
