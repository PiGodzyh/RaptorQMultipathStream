/**
 * @file av_sender.h
 * @brief 音视频发送器
 * 
 * 使用 network 模块进行音视频数据的网络传输
 */

#pragma once

#include "av_codec.h"
#include "../network/network_client.h"
#include <string>
#include <atomic>
#include <thread>
#include <functional>

namespace AVCodecModule {

/**
 * 音视频发送器
 * 读取媒体文件，通过 UDP 发送
 */
class AVSender {
public:
    // 发送进度回调
    using ProgressCallback = std::function<void(uint64_t sent_packets, uint64_t total_bytes)>;
    
public:
    /**
     * 构造函数
     * @param server_addr 目标服务器地址
     * @param server_port 目标服务器端口
     */
    AVSender(const std::string& server_addr, uint16_t server_port);
    ~AVSender();
    
    // 禁止拷贝
    AVSender(const AVSender&) = delete;
    AVSender& operator=(const AVSender&) = delete;
    
    /**
     * 打开媒体文件
     * @param filename 媒体文件路径
     * @return 成功返回 true
     */
    bool open(const std::string& filename);
    
    /**
     * 关闭
     */
    void close();
    
    /**
     * 发送所有数据（阻塞）
     * @return 成功返回 true
     */
    bool sendAll();
    
    /**
     * 开始异步发送
     */
    void startAsync();
    
    /**
     * 停止发送
     */
    void stop();
    
    /**
     * 设置进度回调
     */
    void setProgressCallback(ProgressCallback callback) { progress_callback_ = callback; }
    
    /**
     * 设置发送间隔（毫秒），0表示尽快发送
     */
    void setSendInterval(uint32_t interval_ms) { send_interval_ms_ = interval_ms; }
    
    /**
     * 获取媒体信息
     */
    const MediaInfo& getMediaInfo() const { return reader_.getMediaInfo(); }
    
    /**
     * 获取已发送的包数
     */
    uint64_t getSentPackets() const { return sent_packets_; }
    
    /**
     * 获取已发送的字节数
     */
    uint64_t getSentBytes() const { return sent_bytes_; }
    
    /**
     * 是否正在运行
     */
    bool isRunning() const { return running_; }
    
    /**
     * 是否已打开文件
     */
    bool isOpen() const { return reader_.isOpen(); }
    
private:
    bool sendHeader();
    bool sendPacket(const MediaPacket& packet);
    bool sendFragmented(const std::vector<uint8_t>& data, uint32_t packet_index);
    
private:
    std::string server_addr_;
    uint16_t server_port_;
    
    MediaReader reader_;
    Network::UDPClient client_;
    
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> sent_packets_{0};
    std::atomic<uint64_t> sent_bytes_{0};
    
    uint32_t send_interval_ms_ = 0;
    ProgressCallback progress_callback_;
    
    std::thread send_thread_;
};

} // namespace AVCodecModule
