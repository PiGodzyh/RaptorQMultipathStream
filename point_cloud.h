/**
 * point_cloud.h - 点云数据传输模块
 * 
 * 实现 Livox CustomMsg 格式的点云传输：
 * - 发送端：读取点云文件，按100ms时间窗口分块发送
 * - 接收端：接收并保存为PCD文件
 * 
 * FEC策略：10%冗余，8192B符号
 */

#ifndef POINT_CLOUD_H
#define POINT_CLOUD_H

#include "data_common.h"
#include "unified_sender.h"
#include "unified_receiver.h"
#include <atomic>
#include <thread>
#include <fstream>

namespace DataTransmit {

// ============================================================================
// 点云文件读取器
// ============================================================================
class PointCloudReader {
public:
    // 从PCD文件读取点云
    bool ReadFromPCD(const std::string& filepath, std::vector<PointCloudPoint>& points);
    
    // 生成测试点云（用于演示）
    void GenerateTestCloud(std::vector<PointCloudPoint>& points, uint32_t count = 1000);
};

// ============================================================================
// 点云发送器
// ============================================================================
class PointCloudTransmitter {
public:
    PointCloudTransmitter(std::shared_ptr<UnifiedSender> unified_sender);
    ~PointCloudTransmitter();
    
    // 从文件发送点云
    bool SendFromFile(const std::string& filepath);
    
    // 发送测试点云
    void SendTestCloud(uint32_t point_count = 1000, uint32_t frame_count = 10);
    
    // 停止发送
    void Stop();

private:
    // 发送单帧
    bool SendFrame(const PointCloudFrame& frame, uint32_t frame_seq);
    
    std::shared_ptr<UnifiedSender> unified_sender_;
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> frame_counter_{0};
    std::mutex send_mutex_;
};

// ============================================================================
// 点云接收器
// ============================================================================
class PointCloudReceiver {
public:
    PointCloudReceiver(std::shared_ptr<DataTransmit::UnifiedReceiver> unified_receiver);
    ~PointCloudReceiver();
    
    // 启动接收
    void Start();
    
    // 停止接收
    void Stop();
    
    // 保存到PCD文件
    void SetOutputFile(const std::string& filepath);
    
    // 获取统计
    struct Stats {
        uint32_t frames_received = 0;
        uint32_t points_received = 0;
    };
    Stats GetStats() const { return stats_; }

private:
    // 处理解码完成的帧
    void OnFrameReceived(DataPriority priority, uint32_t stream_id, 
                         const std::vector<uint8_t>& data);
    
    // 保存点云到文件
    void SavePointCloud(const std::vector<PointCloudPoint>& points);
    
    std::shared_ptr<DataTransmit::UnifiedReceiver> unified_receiver_;
    int callback_id_ = -1;  // 回调注册ID
    std::atomic<bool> running_{false};
    std::string output_path_;
    std::ofstream output_file_;
    std::mutex file_mutex_;
    
    Stats stats_;
    std::vector<PointCloudPoint> accumulated_points_;
    std::mutex points_mutex_;
};

} // namespace DataTransmit

#endif // POINT_CLOUD_H
