/**
 * @file av_receiver.h
 * @brief 音视频接收器
 * 
 * 使用 network 模块接收音视频数据并写入文件
 */

#pragma once

#include "av_codec.h"
#include "../network/network_server.h"
#include <string>
#include <atomic>
#include <mutex>
#include <queue>
#include <map>
#include <functional>

namespace AVCodecModule {

/**
 * 音视频接收器
 * 通过 UDP 接收音视频数据，写入文件
 */
class AVReceiver {
public:
    // 接收进度回调
    using ProgressCallback = std::function<void(uint64_t received_packets, uint64_t total_bytes)>;
    
public:
    /**
     * 构造函数
     * @param port 监听端口
     */
    explicit AVReceiver(uint16_t port);
    ~AVReceiver();
    
    // 禁止拷贝
    AVReceiver(const AVReceiver&) = delete;
    AVReceiver& operator=(const AVReceiver&) = delete;
    
    /**
     * 设置输出文件
     * @param filename 输出文件路径
     */
    void setOutputFile(const std::string& filename) { output_file_ = filename; }
    
    /**
     * 启动接收（阻塞）
     * @return 成功返回 true
     */
    bool start();
    
    /**
     * 启动接收（非阻塞）
     */
    void startAsync();
    
    /**
     * 停止接收
     */
    void stop();
    
    /**
     * 完成输出（刷新缓冲区，关闭文件）
     */
    void finalize();
    
    /**
     * 设置进度回调
     */
    void setProgressCallback(ProgressCallback callback) { progress_callback_ = callback; }
    
    /**
     * 设置最大缓冲包数
     */
    void setMaxBufferSize(size_t size) { max_buffer_size_ = size; }
    
    /**
     * 获取接收到的包数
     */
    uint64_t getReceivedPackets() const { return received_packets_; }
    
    /**
     * 获取接收到的字节数
     */
    uint64_t getReceivedBytes() const { return received_bytes_; }
    
    /**
     * 获取写入的包数
     */
    uint64_t getWrittenPackets() const { return writer_.getPacketCount(); }
    
    /**
     * 是否正在运行
     */
    bool isRunning() const { return running_; }
    
    /**
     * 是否已收到媒体头
     */
    bool isHeaderReceived() const { return header_received_; }
    
private:
    void onReceive(std::shared_ptr<Network::Packet> packet);
    void processPacket(const MediaPacket& packet);
    void processCompleteData(const std::vector<uint8_t>& data);
    void flushBuffer();
    
    // 分片重组相关
    struct FragmentInfo {
        uint32_t total_size;
        uint16_t fragment_count;
        std::map<uint16_t, std::vector<uint8_t>> fragments;
        std::chrono::steady_clock::time_point first_time;
    };
    
    bool tryReassemble(uint32_t packet_index, FragmentInfo& info, std::vector<uint8_t>& result);
    void cleanupOldFragments();
    
private:
    uint16_t port_;
    std::string output_file_;
    
    Network::UDPServer server_;
    MediaWriter writer_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> header_received_{false};
    std::atomic<bool> writer_ready_{false};
    std::atomic<bool> finalized_{false};
    std::atomic<bool> eos_received_{false};  // 收到结束标记
    
    std::atomic<uint64_t> received_packets_{0};
    std::atomic<uint64_t> received_bytes_{0};
    
    // 分片重组缓冲
    std::map<uint32_t, FragmentInfo> fragment_buffers_;
    std::mutex fragment_mutex_;
    
    // 包缓冲区（用于处理乱序）
    struct PacketComparator {
        bool operator()(const MediaPacket& a, const MediaPacket& b) const {
            return a.index > b.index;  // 最小堆
        }
    };
    std::priority_queue<MediaPacket, std::vector<MediaPacket>, PacketComparator> packet_buffer_;
    std::mutex buffer_mutex_;
    uint32_t next_expected_index_ = 0;
    size_t max_buffer_size_ = 100;
    size_t max_wait_packets_ = 30;
    
    ProgressCallback progress_callback_;
    std::mutex writer_mutex_;
};

} // namespace AVCodecModule
