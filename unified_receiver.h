#pragma once

#include "receiver.h"
#include "receiver_center.h"
#include "reorder_buffer.h"
#include "stream_config.h"
#include <thread>
#include <atomic>
#include <map>
#include <functional>

/**
 * UnifiedReceiver - 统一接收端
 * 
 * 对应 UnifiedSender，同时监听 5 个端口（9000-9004）
 * 负责：多端口接收、块重组、保序、按优先级分发
 */

namespace DataTransmit {

// 块类型（与 BlockPartition 对应）
enum class ReceivedBlockType : uint8_t {
    SINGLE = 0,       // 单帧块
    SPLIT_START = 1,  // 拆分开始
    SPLIT_MIDDLE = 2, // 拆分中间
    SPLIT_END = 3,    // 拆分结束
    AGGREGATED = 4    // 聚合块
};

// 接收到的块结构
struct ReceivedBlock {
    uint32_t block_id;
    ReceivedBlockType type;
    DataPriority priority;
    uint32_t stream_id;
    uint32_t total_blocks;
    uint32_t block_index;
    std::vector<uint8_t> data;
    
    // 用于重组的元数据
    bool is_first_block;
    bool is_last_block;
};

// 拆分重组状态（支持乱序）
struct SplitReassemblyState {
    uint32_t base_stream_id;      // 原始帧的 stream_id
    uint32_t total_blocks;        // 总分块数
    DataPriority priority;        // 优先级
    std::map<uint32_t, std::vector<uint8_t>> blocks;  // block_index -> data
    std::chrono::steady_clock::time_point first_block_time;
    
    SplitReassemblyState() : base_stream_id(0), total_blocks(0), 
                             priority(DataPriority::GRID_MAP) {}
};

/**
 * BlockReassembler - 块重组器
 * 
 * 功能：
 * 1. 处理聚合块拆分（AGGREGATED）：按4字节长度前缀拆分成多帧
 * 2. 处理分块重组（SPLIT）：按 base_stream_id 缓存重组，支持乱序
 * 
 * 对于 SPLIT 类型，当收齐所有分块后，输出完整重组帧
 */
class BlockReassembler {
public:
    using ReassembleCallback = std::function<void(DataPriority, uint32_t, 
                                                   const std::vector<uint8_t>&)>;
    
    BlockReassembler();
    ~BlockReassembler();
    
    // 处理接收到的块
    // 对于 SINGLE/AGGREGATED：立即输出
    // 对于 SPLIT：缓存等待，收齐后输出
    void processBlock(const ReceivedBlock& block, ReassembleCallback callback);
    
    // 清理超时未完成的重组（默认 5 秒超时）
    void cleanupStaleReassembly(uint64_t timeout_ms = 5000);
    
    // 获取统计信息
    struct Stats {
        uint64_t single_blocks = 0;
        uint64_t aggregated_blocks = 0;
        uint64_t split_blocks = 0;
        uint64_t reassembled_frames = 0;
        uint64_t dropped_timeouts = 0;
        uint64_t pending_reassemblies = 0;  // 当前正在重组的任务数
    };
    Stats getStats() const;
    
private:
    // 处理聚合块（拆分多帧）
    void processAggregatedBlock(const ReceivedBlock& block, ReassembleCallback callback);
    
    // 处理分块（缓存等待重组）
    void processSplitBlock(const ReceivedBlock& block, ReassembleCallback callback);
    
    // 检查是否收齐所有分块，收齐则输出
    void checkAndReassemble(uint32_t base_stream_id, ReassembleCallback callback);
    
    // 重组缓冲区（base_stream_id -> 重组状态）
    mutable std::mutex reassembly_mutex_;  // mutable for getStats
    std::map<uint32_t, SplitReassemblyState> split_states_;
    
    // 统计
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

/**
 * UnifiedReceiver - 统一接收端
 * 
 * 同时监听 5 个端口（对应 UnifiedSender 的 5 个 Sender）
 * 自动根据端口识别 DataPriority
 */
class UnifiedReceiver {
public:
    // 解码完成回调（已重组的完整帧）
    using DecodeCallback = std::function<void(
        DataPriority priority,      // 数据优先级（对应端口）
        uint32_t stream_id,         // 流 ID（原始帧的 stream_id）
        const std::vector<uint8_t>& data  // 解码并重组后的完整数据
    )>;
    
    // 错误回调
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    // 构造函数：绑定基端口（默认 9000，其他端口依次+1）
    explicit UnifiedReceiver(uint16_t base_port = 9000);
    ~UnifiedReceiver();
    
    // 设置回调（必须在 start 之前设置）
    void setDecodeCallback(DecodeCallback callback);
    void setErrorCallback(ErrorCallback callback);
    
    // 多接收器支持：注册/注销回调（每个接收器独立注册）
    int registerDecodeCallback(DecodeCallback callback);
    void unregisterDecodeCallback(int id);
    
    // 启动/停止所有接收端口
    bool start();
    void stop();
    
    // 获取统计信息
    struct Statistics {
        uint64_t total_packets_received = 0;
        uint64_t total_frames_decoded = 0;
        uint64_t total_bytes_received = 0;
        std::map<DataPriority, uint64_t> per_priority_frames;
    };
    Statistics getStatistics() const;
    void printStatistics() const;
    
private:
    // 内部 Visitor 类（前置声明）
    class InternalVisitor;
    
    // 端口到优先级的映射（与 UnifiedSender 对应）
    static DataPriority portToPriority(uint16_t port);
    static uint16_t priorityToPort(DataPriority priority);
    
    // 处理接收到的 RaptorQ 解码后数据
    void onDecodedData(DataPriority priority, uint32_t stream_id, 
                       const std::vector<uint8_t>& data);
    
    // 块重组后的回调
    void onReassembledData(DataPriority priority, uint32_t stream_id,
                          const std::vector<uint8_t>& data);
    
    // 成员变量
    uint16_t base_port_;
    std::atomic<bool> running_{false};
    
    // 5 个接收器（每个端口一个）
    struct PortReceiver {
        uint16_t port;
        DataPriority priority;
        std::unique_ptr<Receiver> receiver;
    };
    std::vector<PortReceiver> port_receivers_;
    
    // Visitor 列表（保持生命周期）
    std::vector<std::shared_ptr<InternalVisitor>> visitors_;
    
    // 块重组器
    std::unique_ptr<BlockReassembler> reassembler_;
    std::thread reassembly_cleanup_thread_;
    
    // 回调（支持多接收器）
    mutable std::mutex callback_mutex_;
    std::map<int, DecodeCallback> decode_callbacks_;  // 多回调注册表
    int next_callback_id_ = 0;
    DecodeCallback legacy_decode_callback_;  // 兼容旧接口
    ErrorCallback error_callback_;
    
    // 统计
    mutable std::mutex stats_mutex_;
    Statistics stats_;
};

} // namespace DataTransmit
