/**
 * voice_receiver.h - 语音传输接收器
 * 
 * 接收 RaptorQ 解码后的音频数据，写入文件
 * 端口：9004
 */

#ifndef VOICE_RECEIVER_H
#define VOICE_RECEIVER_H

#include "data_common.h"
#include "receiver.h"
#include "VoiceCodec/voice_reader.h"
#include <atomic>
#include <thread>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>

namespace VoiceReceive {

// 接收帧结构
struct ReceiveFrame {
    uint32_t seq;                   // 帧序号
    uint64_t timestamp;             // 时间戳
    std::vector<uint8_t> pcm_data;  // PCM 数据
};

// 接收器 Visitor 实现
class VoiceReceiverVisitor : public Receiver::Visitor {
public:
    VoiceReceiverVisitor() = default;
    
    void SetCallback(std::function<void(uint32_t, const std::vector<uint8_t>&)> callback) {
        callback_ = callback;
    }
    
    void OnDecodeComplete(uint32_t stream_id, const std::vector<uint8_t>& data) override {
        if (callback_) {
            callback_(stream_id, data);
        }
    }

private:
    std::function<void(uint32_t, const std::vector<uint8_t>&)> callback_;
};

// 语音接收器
class VoiceReceiver {
public:
    /**
     * 构造函数
     * @param local_port 监听端口（默认9004）
     */
    explicit VoiceReceiver(uint16_t local_port = 9004);
    ~VoiceReceiver();
    
    /**
     * 创建输出文件
     * @param filepath 输出文件路径
     * @return 是否成功
     */
    bool CreateOutput(const std::string& filepath);
    
    /**
     * 开始接收
     */
    void Start();
    
    /**
     * 停止接收
     */
    void Stop();
    
    /**
     * 检查是否正在运行
     */
    bool IsRunning() const { return running_; }
    
    /**
     * 获取统计信息
     */
    struct Stats {
        uint32_t frames_received = 0;   // 接收帧数
        uint32_t bytes_received = 0;    // 接收字节数
        uint32_t packets_lost = 0;      // 丢包数
        uint64_t start_time_us = 0;     // 开始时间
    };
    Stats GetStats() const { return stats_; }

private:
    // 处理接收到的数据
    void OnDataReceived(uint32_t stream_id, const std::vector<uint8_t>& data);
    
    // 处理线程（解码 + 写入文件）
    void ProcessLoop();
    
    // 解析配置包
    bool ParseConfig(const std::vector<uint8_t>& data);
    
    // 解析数据包
    ReceiveFrame ParsePacket(const std::vector<uint8_t>& data);
    
    // 初始化写入器
    bool InitWriter();
    
    VoiceReceiverVisitor visitor_;
    std::unique_ptr<Receiver> receiver_;
    std::unique_ptr<VoiceCodec::VoiceWriter> writer_;
    
    uint16_t local_port_;
    std::string output_path_;
    
    // 音频参数（从配置包获取）
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    uint16_t bits_per_sample_ = 0;
    uint32_t expected_total_frames_ = 0;
    bool config_received_ = false;
    std::mutex config_mutex_;
    
    std::atomic<bool> running_{false};
    
    // 接收队列
    std::queue<std::vector<uint8_t>> receive_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 序号管理（按序写入）
    uint32_t next_write_seq_ = 0;
    std::map<uint32_t, ReceiveFrame> frame_buffer_;  // 乱序帧缓存
    const size_t kMaxBufferSize = 100;  // 最大缓存帧数
    
    // 统计
    Stats stats_;
    mutable std::mutex stats_mutex_;
    
    std::thread process_thread_;
};

} // namespace VoiceReceive

#endif // VOICE_RECEIVER_H
