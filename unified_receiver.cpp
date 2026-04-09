#include "unified_receiver.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <algorithm>

namespace DataTransmit {

// 辅助函数：DataPriority 转字符串
static const char* PriorityToString(DataPriority prio) {
    switch (prio) {
        case DataPriority::FC_COMMAND: return "FC_COMMAND";
        case DataPriority::VOICE: return "VOICE";
        case DataPriority::VIDEO: return "VIDEO";
        case DataPriority::POINT_CLOUD: return "POINT_CLOUD";
        case DataPriority::GRID_MAP: return "GRID_MAP";
        default: return "UNKNOWN";
    }
}

// ============================================================================
// BlockReassembler 实现
// ============================================================================

BlockReassembler::BlockReassembler() {}

BlockReassembler::~BlockReassembler() {}

void BlockReassembler::processBlock(const ReceivedBlock& block, ReassembleCallback callback) {
    switch (block.type) {
        case ReceivedBlockType::SINGLE:
            // 单帧块：直接透传
            if (callback) {
                callback(block.priority, block.stream_id, block.data);
            }
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.single_blocks++;
            }
            break;
            
        case ReceivedBlockType::AGGREGATED:
            // 聚合块：需要拆分
            processAggregatedBlock(block, callback);
            break;
            
        case ReceivedBlockType::SPLIT_START:
        case ReceivedBlockType::SPLIT_MIDDLE:
        case ReceivedBlockType::SPLIT_END:
            // 拆分块：需要重组（支持乱序）
            processSplitBlock(block, callback);
            break;
    }
}

void BlockReassembler::processAggregatedBlock(const ReceivedBlock& block, ReassembleCallback callback) {
    const uint8_t* data = block.data.data();
    size_t offset = 0;
    uint32_t frame_count = 0;
    
    // 按 4 字节长度前缀解析多帧
    while (offset + 4 <= block.data.size()) {
        // 读取长度（大端）
        uint32_t frame_len = (data[offset] << 24) | (data[offset + 1] << 16) | 
                            (data[offset + 2] << 8) | data[offset + 3];
        offset += 4;
        
        if (frame_len == 0 || offset + frame_len > block.data.size()) {
            std::cerr << "[BlockReassembler] Invalid frame length: " << frame_len 
                      << " at offset " << (offset - 4) << std::endl;
            break;
        }
        
        // 提取帧数据并回调
        std::vector<uint8_t> frame_data(data + offset, data + offset + frame_len);
        if (callback) {
            // 聚合帧使用原始 stream_id + 帧序号作为新的 stream_id
            callback(block.priority, block.stream_id * 1000 + frame_count, frame_data);
        }
        frame_count++;
        offset += frame_len;
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.aggregated_blocks++;
        stats_.reassembled_frames += frame_count;
    }
}

void BlockReassembler::processSplitBlock(const ReceivedBlock& block, ReassembleCallback callback) {
    std::lock_guard<std::mutex> lock(reassembly_mutex_);
    
    // 解码 stream_id
    // 编码规则：base_id * 10000 + total_blocks * 1000 + block_index
    uint32_t base_stream_id = block.stream_id / 10000;
    uint32_t remaining = block.stream_id % 10000;
    uint32_t total_blocks = remaining / 1000;
    uint32_t block_index = remaining % 1000;
    
    // 获取或创建重组状态
    auto& state = split_states_[base_stream_id];
    
    // 初始化（第一个分块）
    if (state.blocks.empty()) {
        state.base_stream_id = base_stream_id;
        state.total_blocks = total_blocks;
        state.priority = block.priority;
        state.first_block_time = std::chrono::steady_clock::now();
        
        std::cout << "[BlockReassembler] Start reassembly for stream " << base_stream_id 
                  << ", total_blocks=" << total_blocks << std::endl;
    }
    
    // 存储当前分块
    if (state.blocks.find(block_index) == state.blocks.end()) {
        state.blocks[block_index] = block.data;
        std::cout << "[BlockReassembler] Received block " << block_index 
                  << "/" << state.total_blocks << " for stream " << base_stream_id 
                  << " (" << block.data.size() << " bytes)" << std::endl;
    } else {
        std::cout << "[BlockReassembler] Duplicate block " << block_index 
                  << " for stream " << base_stream_id << ", ignoring" << std::endl;
    }
    
    // 检查是否收齐
    checkAndReassemble(base_stream_id, callback);
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.split_blocks++;
    }
}

void BlockReassembler::checkAndReassemble(uint32_t base_stream_id, ReassembleCallback callback) {
    auto it = split_states_.find(base_stream_id);
    if (it == split_states_.end()) {
        return;
    }
    
    auto& state = it->second;
    
    // 检查是否收齐所有分块
    if (state.blocks.size() == state.total_blocks) {
        // 按序号排序重组
        std::vector<uint8_t> full_data;
        full_data.reserve(state.total_blocks * 500);  // 预估容量
        
        for (uint32_t i = 0; i < state.total_blocks; ++i) {
            auto block_it = state.blocks.find(i);
            if (block_it == state.blocks.end()) {
                std::cerr << "[BlockReassembler] Missing block " << i 
                          << " for stream " << base_stream_id << std::endl;
                return;
            }
            full_data.insert(full_data.end(), 
                           block_it->second.begin(), 
                           block_it->second.end());
        }
        
        std::cout << "[BlockReassembler] Reassembly complete for stream " << base_stream_id 
                  << ", total_size=" << full_data.size() << " bytes" << std::endl;
        
        // 回调完整帧
        if (callback) {
            callback(state.priority, base_stream_id, full_data);
        }
        
        // 清理状态
        split_states_.erase(it);
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.reassembled_frames++;
        }
    }
}

void BlockReassembler::cleanupStaleReassembly(uint64_t timeout_ms) {
    auto now = std::chrono::steady_clock::now();
    
    std::lock_guard<std::mutex> lock(reassembly_mutex_);
    
    for (auto it = split_states_.begin(); it != split_states_.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.first_block_time).count();
        
        if (elapsed > static_cast<int64_t>(timeout_ms)) {
            std::cerr << "[BlockReassembler] Timeout for stream " << it->first 
                      << ", received " << it->second.blocks.size() 
                      << "/" << it->second.total_blocks << " blocks" << std::endl;
            it = split_states_.erase(it);
            
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.dropped_timeouts++;
        } else {
            ++it;
        }
    }
}

BlockReassembler::Stats BlockReassembler::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    std::lock_guard<std::mutex> reassembly_lock(reassembly_mutex_);
    Stats s = stats_;
    s.pending_reassemblies = split_states_.size();
    return s;
}

// ============================================================================
// UnifiedReceiver 实现
// ============================================================================

// 内部 Visitor 类，用于接收回调
class UnifiedReceiver::InternalVisitor : public Receiver::Visitor {
public:
    InternalVisitor(UnifiedReceiver* parent, DataPriority priority) 
        : parent_(parent), priority_(priority) {}
    
    void OnDecodeComplete(uint32_t stream_id, const std::vector<uint8_t>& data) override {
        if (parent_) {
            parent_->onDecodedData(priority_, stream_id, data);
        }
    }
    
private:
    UnifiedReceiver* parent_;
    DataPriority priority_;
};

UnifiedReceiver::UnifiedReceiver(uint16_t base_port)
    : base_port_(base_port)
    , reassembler_(std::make_unique<BlockReassembler>()) {
}

UnifiedReceiver::~UnifiedReceiver() {
    stop();
}

DataPriority UnifiedReceiver::portToPriority(uint16_t port) {
    switch (port) {
        case 9000: return DataPriority::FC_COMMAND;
        case 9001: return DataPriority::VIDEO;
        case 9002: return DataPriority::POINT_CLOUD;
        case 9003: return DataPriority::GRID_MAP;
        case 9004: return DataPriority::VOICE;
        default: return DataPriority::GRID_MAP;
    }
}

uint16_t UnifiedReceiver::priorityToPort(DataPriority priority) {
    switch (priority) {
        case DataPriority::FC_COMMAND: return 9000;
        case DataPriority::VIDEO: return 9001;
        case DataPriority::POINT_CLOUD: return 9002;
        case DataPriority::GRID_MAP: return 9003;
        case DataPriority::VOICE: return 9004;
        default: return 9000;
    }
}

void UnifiedReceiver::setDecodeCallback(DecodeCallback callback) {
    decode_callback_ = callback;
}

void UnifiedReceiver::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

bool UnifiedReceiver::start() {
    if (running_) {
        return true;
    }
    
    running_ = true;
    
    // 创建 5 个端口接收器
    uint16_t ports[] = {9000, 9001, 9002, 9003, 9004};
    
    for (int i = 0; i < 5; ++i) {
        PortReceiver pr;
        pr.port = ports[i];
        pr.priority = portToPriority(ports[i]);
        
        // 创建 Visitor
        auto visitor = std::make_shared<InternalVisitor>(this, pr.priority);
        visitors_.push_back(visitor);
        
        // 创建 Receiver（每个端口一个）
        pr.receiver = std::make_unique<Receiver>(visitor.get(), ports[i], 1);
        
        std::cout << "[UnifiedReceiver] Starting receiver on port " << ports[i] 
                  << " for " << PriorityToString(pr.priority) << std::endl;
        
        // 启动接收
        pr.receiver->start();
        port_receivers_.push_back(std::move(pr));
    }
    
    // 启动重组清理线程
    reassembly_cleanup_thread_ = std::thread([this]() {
        while (running_) {
            reassembler_->cleanupStaleReassembly(5000);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
    
    std::cout << "[UnifiedReceiver] All 5 receivers started" << std::endl;
    return true;
}

void UnifiedReceiver::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // 停止所有接收器
    for (auto& pr : port_receivers_) {
        if (pr.receiver) {
            pr.receiver->stop();
        }
    }
    port_receivers_.clear();
    visitors_.clear();
    
    // 停止清理线程
    if (reassembly_cleanup_thread_.joinable()) {
        reassembly_cleanup_thread_.join();
    }
    
    std::cout << "[UnifiedReceiver] All receivers stopped" << std::endl;
}

void UnifiedReceiver::onDecodedData(DataPriority priority, uint32_t stream_id, 
                                   const std::vector<uint8_t>& data) {
    // 更新统计
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_packets_received++;
        stats_.total_bytes_received += data.size();
    }
    
    // 构造 ReceivedBlock
    // 判断是否为分块：stream_id >= 10000 表示分块（base_id * 10000 + total * 1000 + index）
    ReceivedBlock block;
    block.priority = priority;
    block.stream_id = stream_id;
    
    if (stream_id >= 10000) {
        // 分块数据
        block.type = ReceivedBlockType::SPLIT_MIDDLE;
        block.total_blocks = 0;  // 由重组器从stream_id解码
        block.block_index = 0;   // 由重组器从stream_id解码
    } else {
        // 单帧数据
        block.type = ReceivedBlockType::SINGLE;
        block.total_blocks = 1;
        block.block_index = 0;
    }
    block.data = data;
    
    // 通过重组器处理（处理聚合/拆分）
    reassembler_->processBlock(block, [this](DataPriority prio, uint32_t sid, 
                                              const std::vector<uint8_t>& frame_data) {
        this->onReassembledData(prio, sid, frame_data);
    });
}

void UnifiedReceiver::onReassembledData(DataPriority priority, uint32_t stream_id,
                                       const std::vector<uint8_t>& data) {
    // 更新统计
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_frames_decoded++;
        stats_.per_priority_frames[priority]++;
    }
    
    // 回调用户
    if (decode_callback_) {
        decode_callback_(priority, stream_id, data);
    }
}

UnifiedReceiver::Statistics UnifiedReceiver::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void UnifiedReceiver::printStatistics() const {
    auto stats = getStatistics();
    auto reassembler_stats = reassembler_->getStats();
    
    std::cout << "========================================" << std::endl;
    std::cout << "  UnifiedReceiver Statistics" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total packets: " << stats.total_packets_received << std::endl;
    std::cout << "Total frames decoded: " << stats.total_frames_decoded << std::endl;
    std::cout << "Total bytes: " << stats.total_bytes_received << std::endl;
    std::cout << "Per priority:" << std::endl;
    for (const auto& pair : stats.per_priority_frames) {
        std::cout << "  " << PriorityToString(pair.first) << ": " << pair.second << std::endl;
    }
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Reassembler Stats:" << std::endl;
    std::cout << "  Single blocks: " << reassembler_stats.single_blocks << std::endl;
    std::cout << "  Aggregated blocks: " << reassembler_stats.aggregated_blocks << std::endl;
    std::cout << "  Split blocks: " << reassembler_stats.split_blocks << std::endl;
    std::cout << "  Reassembled frames: " << reassembler_stats.reassembled_frames << std::endl;
    std::cout << "  Pending reassemblies: " << reassembler_stats.pending_reassemblies << std::endl;
    std::cout << "========================================" << std::endl;
}

} // namespace DataTransmit
