/**
 * voice_transmitter.h - 语音传输发送器
 * 
 * 读取音频文件，RaptorQ 编码后发送
 * 端口：9004
 */

#ifndef VOICE_TRANSMITTER_H
#define VOICE_TRANSMITTER_H

#include "data_common.h"
#include "sender.h"
#include "VoiceCodec/voice_reader.h"
#include <atomic>
#include <thread>
#include <memory>
#include <string>

namespace VoiceTransmit {

// 语音流配置头部（每个流开始时发送，stream_id=0）
struct VoiceConfigHeader {
    uint32_t sample_rate;           // 采样率 (Hz)
    uint16_t channels;              // 声道数
    uint16_t bits_per_sample;       // 位深
    uint32_t total_frames;          // 总帧数
    
    static constexpr uint32_t kHeaderSize = sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint32_t);
};

// 语音帧头部（用于网络传输）
struct VoiceFrameHeader {
    uint32_t seq;                   // 帧序号
    uint64_t timestamp;             // 时间戳（微秒）
    uint32_t data_size;             // PCM 数据大小
    
    static constexpr uint32_t kHeaderSize = sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint32_t);
};

// 语音发送器
class VoiceTransmitter {
public:
    /**
     * 构造函数
     * @param server_addr 目标地址
     * @param server_port 目标端口（默认9004）
     */
    VoiceTransmitter(const std::string& server_addr, 
                     uint16_t server_port = 9004);
    ~VoiceTransmitter();
    
    /**
     * 打开音频文件
     * @param filepath 音频文件路径（WAV/PCM）
     * @return 是否成功
     */
    bool OpenFile(const std::string& filepath);
    
    /**
     * 开始传输
     */
    void Start();
    
    /**
     * 停止传输
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
        uint32_t frames_sent = 0;       // 发送帧数
        uint32_t bytes_sent = 0;        // 发送字节数
        uint64_t start_time_us = 0;     // 开始时间
    };
    Stats GetStats() const { return stats_; }
    
    /**
     * 获取音频参数
     */
    uint32_t GetTotalFrames() const;
    uint32_t GetCurrentFrame() const;

private:
    // 发送循环
    void TransmitLoop();
    
    // 发送配置
    void SendConfig();
    
    // 发送单帧
    void SendFrame(const VoiceCodec::VoiceFrame& audio_frame);
    
    // 构建网络包
    std::vector<uint8_t> BuildPacket(const VoiceCodec::VoiceFrame& audio_frame);
    
    std::unique_ptr<Sender> sender_;
    std::unique_ptr<VoiceCodec::VoiceReader> reader_;
    
    std::string server_addr_;
    uint16_t server_port_;
    std::string filepath_;
    
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> seq_counter_{0};
    
    Stats stats_;
    mutable std::mutex stats_mutex_;
    
    std::thread transmit_thread_;
};

} // namespace VoiceTransmit

#endif // VOICE_TRANSMITTER_H
