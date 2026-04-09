#include "block_partition.h"
#include <iostream>
#include <algorithm>

BlockPartition::BlockPartition(const BlockPolicy& policy) : policy_(policy) {}

bool BlockPartition::addFrame(DataPriority priority, uint64_t stream_id, 
                               uint64_t seq, std::shared_ptr<std::string> data) {
    if (!data || data->empty()) return false;
    
    PendingFrame frame(seq, data, priority, stream_id);
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_frames_in++;
    }
    
    size_t data_size = data->size();
    
    if (data_size > policy_.max_block_size) {
        splitLargeFrame(frame);
    } else {
        // 所有小于 max_block_size 的帧都直接发送，不做聚合等待
        // 聚合会导致延迟，对实时数据不利
        createSingleBlock(frame);
    }
    
    return true;
}

void BlockPartition::splitLargeFrame(const PendingFrame& frame) {
    size_t data_size = frame.data->size();
    size_t offset = 0;
    uint32_t block_index = 0;
    uint32_t total_blocks = (data_size + policy_.max_block_size - 1) / policy_.max_block_size;
    
    // 基础 stream_id，用于重组时识别同一帧的分块
    uint32_t base_stream_id = frame.stream_id;
    
    while (offset < data_size) {
        SourceBlock block;
        block.block_id = getNextBlockId();
        block.priority = frame.priority;
        // 每个分块使用独立的 stream_id，避免 RaptorQ 流冲突
        // 编码规则：base_id * 10000 + total_blocks * 1000 + block_index
        // 这样接收端可以从 stream_id 解码出所有信息
        block.stream_id = base_stream_id * 10000 + total_blocks * 1000 + block_index;
        block.start_seq = frame.seq;
        block.num_frames = 1;
        block.total_blocks = total_blocks;
        block.block_index = block_index;
        block.is_first_block = (block_index == 0);
        block.is_last_block = (block_index == total_blocks - 1);
        
        if (total_blocks == 1) {
            block.type = BlockType::SINGLE;
        } else if (block_index == 0) {
            block.type = BlockType::SPLIT_START;
        } else if (block_index == total_blocks - 1) {
            block.type = BlockType::SPLIT_END;
        } else {
            block.type = BlockType::SPLIT_MIDDLE;
        }
        
        size_t chunk_size = std::min(policy_.max_block_size, data_size - offset);
        block.data.assign(frame.data->begin() + offset, 
                          frame.data->begin() + offset + chunk_size);
        
        {
            std::lock_guard<std::mutex> lock(output_mutex_);
            output_queue_.push_back(std::move(block));
        }
        
        offset += chunk_size;
        block_index++;
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.split_frames++;
        stats_.total_blocks_out += total_blocks;
    }
    
    std::cout << "[BlockPartition] Split frame " << frame.seq 
              << " into " << total_blocks << " blocks" << std::endl;
}

bool BlockPartition::tryAggregateFrame(const PendingFrame& frame) {
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        
        if (pending_frames_.size() >= policy_.max_aggregation) {
            // 缓冲区满
        } else if (!pending_frames_.empty()) {
            const auto& first = pending_frames_.front();
            if (first.priority != frame.priority || first.stream_id != frame.stream_id) {
                // 不同流，需要创建聚合块
            } else {
                pending_frames_.push_back(frame);
                
                size_t total_size = 0;
                for (const auto& f : pending_frames_) {
                    total_size += f.data->size();
                }
                
                if (total_size >= policy_.target_block_size || 
                    pending_frames_.size() >= policy_.max_aggregation) {
                    // 需要创建聚合块
                } else {
                    return true;
                }
            }
        } else {
            pending_frames_.push_back(frame);
            return true;
        }
    }
    
    // 需要创建聚合块后再添加
    createAggregatedBlock();
    
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_frames_.push_back(frame);
    }
    return true;
}

void BlockPartition::createAggregatedBlock() {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    
    if (pending_frames_.empty()) return;
    
    SourceBlock block;
    block.block_id = getNextBlockId();
    block.type = BlockType::AGGREGATED;
    block.priority = pending_frames_.front().priority;
    block.stream_id = pending_frames_.front().stream_id;
    block.start_seq = pending_frames_.front().seq;
    block.num_frames = pending_frames_.size();
    block.is_first_block = true;
    block.is_last_block = true;
    block.total_blocks = 1;
    block.block_index = 0;
    
    // 单帧情况：直接传递原始数据，不添加长度前缀（保持向后兼容）
    if (pending_frames_.size() == 1) {
        const auto& frame = pending_frames_.front();
        block.data.assign(frame.data->begin(), frame.data->end());
    } else {
        // 多帧情况：添加 4 字节长度前缀 + 数据的格式
        size_t total_size = 0;
        for (const auto& frame : pending_frames_) {
            total_size += sizeof(uint32_t) + frame.data->size();
        }
        
        block.data.reserve(total_size);
        
        for (const auto& frame : pending_frames_) {
            uint32_t size = frame.data->size();
            block.data.push_back((size >> 24) & 0xFF);
            block.data.push_back((size >> 16) & 0xFF);
            block.data.push_back((size >> 8) & 0xFF);
            block.data.push_back(size & 0xFF);
            block.data.insert(block.data.end(), frame.data->begin(), frame.data->end());
        }
    }
    
    {
        std::lock_guard<std::mutex> out_lock(output_mutex_);
        output_queue_.push_back(std::move(block));
    }
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.aggregated_frames += pending_frames_.size();
        stats_.total_blocks_out++;
    }
    
    if (pending_frames_.size() == 1) {
        std::cout << "[BlockPartition] Single frame (" << block.data.size() << " bytes)" << std::endl;
    } else {
        std::cout << "[BlockPartition] Aggregated " 
                  << pending_frames_.size() << " frames (" << block.data.size() << " bytes)" << std::endl;
    }
    
    pending_frames_.clear();
}

void BlockPartition::createSingleBlock(const PendingFrame& frame) {
    SourceBlock block;
    block.block_id = getNextBlockId();
    block.type = BlockType::SINGLE;
    block.priority = frame.priority;
    block.stream_id = frame.stream_id;
    block.start_seq = frame.seq;
    block.num_frames = 1;
    block.is_first_block = true;
    block.is_last_block = true;
    block.data.assign(frame.data->begin(), frame.data->end());
    
    {
        std::lock_guard<std::mutex> lock(output_mutex_);
        output_queue_.push_back(std::move(block));
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.single_blocks++;
        stats_.total_blocks_out++;
        size_t size = block.data.size();
        stats_.max_block_size = std::max(stats_.max_block_size, size);
    }
}

bool BlockPartition::getNextBlock(SourceBlock& block) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    
    if (output_queue_.empty()) {
        return false;
    }
    
    block = std::move(output_queue_.front());
    output_queue_.pop_front();
    return true;
}

bool BlockPartition::hasBlock() const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    return !output_queue_.empty();
}

size_t BlockPartition::getQueueSize() const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    return output_queue_.size();
}

size_t BlockPartition::getPendingFramesCount() const {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    return pending_frames_.size();
}

void BlockPartition::flush() {
    createAggregatedBlock();
}

void BlockPartition::setPolicy(const BlockPolicy& policy) {
    policy_ = policy;
}

bool BlockPartition::needFlush() const {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    
    if (pending_frames_.empty()) return false;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - pending_frames_.front().enqueue_time).count();
    
    return elapsed > policy_.aggregation_timeout_ms;
}

uint32_t BlockPartition::getNextBlockId() {
    return block_id_counter_.fetch_add(1);
}

BlockPartition::Statistics BlockPartition::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void BlockPartition::printStatistics() const {
    auto stats = getStatistics();
    
    std::cout << "\n========== BlockPartition Statistics ==========" << std::endl;
    std::cout << "Total frames in: " << stats.total_frames_in << std::endl;
    std::cout << "Total blocks out: " << stats.total_blocks_out << std::endl;
    std::cout << "Split frames: " << stats.split_frames << std::endl;
    std::cout << "Aggregated frames: " << stats.aggregated_frames << std::endl;
    std::cout << "Single blocks: " << stats.single_blocks << std::endl;
    std::cout << "Max block size: " << stats.max_block_size << " bytes" << std::endl;
    std::cout << "==============================================\n" << std::endl;
}

SendTask BlockPartitionAdapter::toSendTask(const SourceBlock& block) {
    SendTask task;
    task.priority = block.priority;
    task.stream_id = block.stream_id;
    task.seq = block.start_seq;
    task.data = std::make_shared<std::string>(block.data.begin(), block.data.end());
    return task;
}
