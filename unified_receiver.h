#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <atomic>

#include "receiver.h"
#include "reorder_buffer.h"
#include "feedback.h"
#include "send_buffer.h"

/**
 * UnifiedReceiver - 统一接收器
 * 
 * 集成：Receiver → ReorderBuffer → 应用层 + FeedbackSender
 */

struct UnifiedReceiverConfig {
    // 反馈配置
    FeedbackSender::Config feedback_config;
    
    // ReorderBuffer 配置
    ReorderConfig reorder_config;
    
    UnifiedReceiverConfig();
};

class UnifiedReceiver : public Receiver::Visitor {
public:
    using DataCallback = std::function<void(DataPriority priority, 
                                            uint64_t stream_id,
                                            const std::vector<uint8_t>& data)>;
    
    explicit UnifiedReceiver(const UnifiedReceiverConfig& config = UnifiedReceiverConfig());
    ~UnifiedReceiver();
    
    /**
     * 初始化
     */
    bool initialize();
    
    /**
     * 启动接收
     */
    void start();
    
    /**
     * 停止接收
     */
    void stop();
    
    /**
     * 设置数据回调（每种优先级可以设置不同的回调）
     */
    void setDataCallback(DataPriority priority, DataCallback callback);
    
    /**
     * 设置统一的回调
     */
    void setDataCallback(DataCallback callback);
    
    /**
     * 发送反馈（网络层调用）
     */
    void sendFeedback(const FeedbackPacket& packet);
    
    /**
     * 打印统计
     */
    void printStatistics() const;
    
    /**
     * 实现 Receiver::Visitor 接口
     */
    void OnDecodeComplete(uint32_t stream_id, 
                          const std::vector<uint8_t>& data) override;

private:
    /**
     * 消费线程：从 ReorderBuffer 取有序数据
     */
    void consumeLoop();
    
    /**
     * 反馈发送线程
     */
    void feedbackLoop();
    
    /**
     * 提取优先级（从数据头部或 stream_id）
     */
    DataPriority extractPriority(const std::vector<uint8_t>& data) const;
    
    /**
     * 获取端口
     */
    uint16_t getPort(DataPriority priority) const;

private:
    UnifiedReceiverConfig config_;
    
    // 子模块
    std::vector<std::unique_ptr<Receiver>> receivers_;
    ReorderBuffer reorder_buffer_;
    FeedbackSender feedback_sender_;
    
    // 回调
    std::vector<DataCallback> callbacks_;
    DataCallback unified_callback_;
    
    // 线程
    std::thread consume_thread_;
    std::thread feedback_thread_;
    std::atomic<bool> running_{false};
};
