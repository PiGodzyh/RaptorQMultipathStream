/**
 * video_receiver.h - 视频接收端
 * 
 * 接收RaptorQ编码的视频数据，解码后写入MP4文件
 * 支持I帧和P/B帧的差异化处理
 */

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <mutex>

#include "receiver.h"
#include "video_common.h"
#include "VideoCodec/video_writer.h"
#include "VideoCodec/video_codec.h"

namespace VideoTransmit {

// 视频接收回调
using VideoFrameCallback = std::function<void(const VideoCodec::EncodedFrame& frame)>;
using VideoConfigCallback = std::function<void(const VideoConfig& config)>;
using VideoErrorCallback = std::function<void(const std::string& error)>;

// 接收端解码器状态
struct SourceBlockState {
    uint32_t block_id;
    uint32_t total_symbols;
    uint32_t received_symbols;
    std::set<uint32_t> symbol_ids;
    std::unique_ptr<RQPack::Decoder> decoder;
    bool decoded;
    std::vector<uint8_t> decoded_data;
    
    SourceBlockState(uint32_t id, size_t data_size, uint16_t symbol_size, uint32_t total)
        : block_id(id)
        , total_symbols(total)
        , received_symbols(0)
        , decoded(false) {
        decoder = std::make_unique<RQPack::Decoder>(data_size, symbol_size);
    }
};

class VideoReceiver : public Receiver::Visitor {
public:
    /**
     * 构造函数
     * @param port 监听端口（默认9001）
     * @param thread_count 接收线程数
     */
    VideoReceiver(uint16_t port = 9001, uint32_t thread_count = 4);
    
    ~VideoReceiver();

    // 禁止拷贝
    VideoReceiver(const VideoReceiver&) = delete;
    VideoReceiver& operator=(const VideoReceiver&) = delete;

    /**
     * 启动接收
     */
    void Start();

    /**
     * 停止接收
     */
    void Stop();

    /**
     * 创建输出文件
     * @param filepath 输出文件路径
     * @return 是否成功
     */
    bool CreateOutputFile(const std::string& filepath);

    /**
     * 关闭输出文件
     */
    void CloseOutputFile();

    /**
     * 设置回调
     */
    void SetFrameCallback(VideoFrameCallback callback);
    void SetConfigCallback(VideoConfigCallback callback);
    void SetErrorCallback(VideoErrorCallback callback);

    /**
     * 检查是否正在运行
     */
    bool IsRunning() const { return running_; }

    /**
     * 检查是否已创建输出文件
     */
    bool IsOutputOpen() const;

    /**
     * 获取统计信息
     */
    VideoTransmitStats GetStatistics() const;

    /**
     * 获取视频配置
     */
    bool GetVideoConfig(VideoConfig& config) const;

private:
    // Receiver::Visitor 接口实现
    void OnDecodeComplete(uint32_t stream_id, const std::vector<uint8_t>& data) override;

    // 内部处理函数
    void ProcessDecodedData(uint32_t stream_id, const std::vector<uint8_t>& data);
    void ProcessVideoConfig(const std::vector<uint8_t>& data);
    void ProcessVideoFrame(const std::vector<uint8_t>& data);
    
    // 获取或创建Source Block状态
    SourceBlockState* GetOrCreateBlockState(uint32_t block_id, size_t data_size, 
                                             uint16_t symbol_size, uint32_t total_symbols);
    
    // 报告错误
    void ReportError(const std::string& error);

    // 网络接收器
    std::unique_ptr<Receiver> receiver_;
    uint16_t port_;
    
    // 视频写入器
    std::unique_ptr<VideoCodec::VideoWriter> video_writer_;
    bool output_opened_;
    
    // 视频配置
    VideoConfig video_config_;
    std::vector<uint8_t> extradata_;
    bool config_received_;
    mutable std::mutex config_mutex_;
    
    // 运行状态
    std::atomic<bool> running_;
    
    // 回调
    VideoFrameCallback frame_callback_;
    VideoConfigCallback config_callback_;
    VideoErrorCallback error_callback_;
    
    // Source Block状态管理
    std::map<uint32_t, std::unique_ptr<SourceBlockState>> block_states_;
    std::mutex block_mutex_;
    
    // 帧序列管理（保序）
    uint32_t next_expected_frame_seq_;
    std::map<uint32_t, VideoCodec::EncodedFrame> pending_frames_;
    std::mutex frame_mutex_;
    
    // 统计
    VideoTransmitStats stats_;
    mutable std::mutex stats_mutex_;
    
    // 帧接收详细统计
    struct FrameStats {
        uint64_t frames_written = 0;        // 成功写入文件的帧
        uint64_t frames_dropped_full = 0;   // 因缓存满丢弃的帧
        uint64_t frames_dropped_old = 0;    // 因过时丢弃的帧
        uint64_t frames_cached = 0;         // 当前缓存的帧数
        
        void Print(const std::string& prefix = "") const {
            uint64_t total = frames_written + frames_dropped_full + frames_dropped_old;
            std::cout << prefix << "[FrameStats] 总计:" << total 
                      << " 成功:" << frames_written 
                      << " 丢弃(满):" << frames_dropped_full 
                      << " 丢弃(旧):" << frames_dropped_old
                      << " 缓存:" << frames_cached
                      << " (成功率:" << (total > 0 ? (frames_written * 100 / total) : 0) << "%)"
                      << std::endl;
        }
    };
    FrameStats frame_stats_;
    mutable std::mutex frame_stats_mutex_;
};

} // namespace VideoTransmit
