/**
 * video_transmitter.h - 视频发送端
 * 
 * 从MP4文件读取H.264编码数据，通过RaptorQ FEC编码后网络传输
 * 支持I帧和P/B帧的差异化处理
 */

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "sender.h"
#include "video_common.h"
#include "VideoCodec/video_reader.h"
#include "VideoCodec/video_codec.h"

namespace VideoTransmit {

// 视频发送状态回调
using VideoSendCallback = std::function<void(uint32_t frame_seq, FrameType type, 
                                              size_t bytes_sent, bool success)>;
using VideoErrorCallback = std::function<void(const std::string& error)>;

class VideoTransmitter {
public:
    /**
     * 构造函数
     * @param server_addr 目标服务器地址
     * @param server_port 目标服务器端口（默认9001视频端口）
     * @param encode_threads 编码线程数
     */
    VideoTransmitter(const std::string& server_addr, 
                     uint16_t server_port = 9001,
                     uint32_t encode_threads = 4);
    
    ~VideoTransmitter();

    // 禁止拷贝
    VideoTransmitter(const VideoTransmitter&) = delete;
    VideoTransmitter& operator=(const VideoTransmitter&) = delete;

    /**
     * 打开视频文件
     * @param filepath MP4文件路径
     * @return 是否成功
     */
    bool OpenVideoFile(const std::string& filepath);

    /**
     * 关闭视频文件
     */
    void CloseVideoFile();

    /**
     * 启动传输
     */
    void Start();

    /**
     * 停止传输
     */
    void Stop();

    /**
     * 设置状态回调
     */
    void SetSendCallback(VideoSendCallback callback);
    void SetErrorCallback(VideoErrorCallback callback);

    /**
     * 检查是否正在运行
     */
    bool IsRunning() const { return running_; }

    /**
     * 检查是否已打开视频文件
     */
    bool IsVideoOpen() const;

    /**
     * 获取统计信息
     */
    VideoTransmitStats GetStatistics() const;

    /**
     * 获取视频信息
     */
    VideoCodec::VideoInfo GetVideoInfo() const;

private:
    // 视频读取器
    std::unique_ptr<VideoCodec::VideoReader> video_reader_;
    bool video_opened_;
    
    // 网络发送器
    std::unique_ptr<Sender> sender_;
    std::string server_addr_;
    uint16_t server_port_;
    
    // 运行状态
    std::atomic<bool> running_;
    std::atomic<bool> send_thread_running_;
    
    // 发送线程
    std::thread send_thread_;
    
    // 回调
    VideoSendCallback send_callback_;
    VideoErrorCallback error_callback_;
    
    // 序列号生成
    std::atomic<uint32_t> frame_seq_counter_;
    std::atomic<uint32_t> gop_counter_;
    std::atomic<uint32_t> block_id_counter_;
    
    // 统计
    VideoTransmitStats stats_;
    mutable std::mutex stats_mutex_;
    
    // 当前GOP状态
    uint32_t current_gop_id_;
    uint32_t frame_in_gop_;
    
    // 发送线程函数
    void SendThreadFunc();
    
    // 发送视频配置
    bool SendVideoConfig();
    
    // 发送一帧视频
    bool SendFrame(const VideoCodec::EncodedFrame& frame);
    
    // 构建视频帧数据（头部 + H.264数据）
    std::vector<uint8_t> BuildFrameData(const VideoCodec::EncodedFrame& frame,
                                         uint32_t frame_seq);
    
    // 更新统计
    void UpdateStats(FrameType type, size_t bytes_sent, uint32_t symbols);
    
    // 报告错误
    void ReportError(const std::string& error);
};

} // namespace VideoTransmit
