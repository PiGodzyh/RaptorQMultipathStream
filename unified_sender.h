#pragma once

#include <memory>
#include <vector>
#include <string>
#include <atomic>

#include "send_buffer.h"
#include "scheduler.h"
#include "block_partition.h"
#include "feedback.h"
#include "sender.h"

/**
 * UnifiedSender - 统一发送器
 * 
 * 集成：BlockPartition → SendBuffer → Scheduler → Sender → Feedback
 */

struct UnifiedSenderConfig {
    // 目标地址
    std::string target_ip = "127.0.0.1";
    
    // BlockPartition 配置
    BlockPolicy block_policy;
    
    // SendBuffer 配置
    SendBuffer::QueueConfig queue_config;
    SendBuffer::ShapingConfig shaping_config;
    
    // Scheduler 配置
    SchedulerConfig scheduler_config;
    
    // Feedback 配置
    AdaptiveFEC::Config fec_config;
    
    UnifiedSenderConfig();
};

class UnifiedSender {
public:
    explicit UnifiedSender(const UnifiedSenderConfig& config = UnifiedSenderConfig());
    ~UnifiedSender();
    
    /**
     * 初始化所有子模块
     */
    bool initialize();
    
    /**
     * 启动发送（启动调度器和反馈）
     */
    void start();
    
    /**
     * 停止发送
     */
    void stop();
    
    /**
     * 统一发送接口
     * @param priority 数据优先级（决定端口和FEC策略）
     * @param stream_id 流ID
     * @param data 数据内容
     * @return true 成功入队
     */
    bool send(DataPriority priority, uint64_t stream_id, 
              std::shared_ptr<std::string> data);
    
    /**
     * 发送（简化接口）
     */
    bool send(DataPriority priority, uint64_t stream_id, 
              const std::vector<uint8_t>& data);
    
    /**
     * 设置 FEC 冗余度（手动覆盖自适应）
     */
    void setRedundancy(DataPriority priority, float ratio);
    
    /**
     * 设置发送速率限制（kbps，0表示无限制）
     */
    void setRateLimit(DataPriority priority, uint32_t kbps);
    
    /**
     * 接收反馈包（网络层调用）
     */
    void onFeedbackReceived(const FeedbackPacket& feedback);
    
    /**
     * 获取统计信息
     */
    void printStatistics() const;
    
    /**
     * 检查是否正在运行
     */
    bool isRunning() const { return running_; }

private:
    /**
     * 内部处理循环：从 BlockPartition 取块并入队
     */
    void processBlockLoop();
    
    /**
     * 创建 Sender 实例
     */
    std::shared_ptr<Sender> createSender(DataPriority priority);
    
    /**
     * 获取端口
     */
    uint16_t getPort(DataPriority priority) const;

private:
    UnifiedSenderConfig config_;
    
    // 子模块
    BlockPartition block_partition_;
    SendBuffer send_buffer_;
    TransmissionScheduler scheduler_;
    FeedbackController feedback_controller_;
    
    // 5个 Sender（对应5种数据类型）
    std::vector<std::shared_ptr<Sender>> senders_;
    
    // 内部线程
    std::thread block_process_thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> seq_counter_{0};
    
    // 统计
    std::atomic<uint64_t> total_frames_in_{0};
    std::atomic<uint64_t> total_blocks_out_{0};
};
