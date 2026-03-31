/**
 * grid_map.h - 栅格地图传输模块
 * 
 * 实现栅格地图数据（Eigen::Vector3d格式）的传输：
 * - 发送端：读取栅格地图文件，按瓦片分块发送
 * - 接收端：接收并保存为栅格地图文件
 * 
 * FEC策略：20%冗余，4096B符号
 */

#ifndef GRID_MAP_H
#define GRID_MAP_H

#include "data_common.h"
#include "unified_sender.h"
#include "receiver.h"
#include <atomic>
#include <thread>
#include <fstream>

namespace DataTransmit {

// ============================================================================
// 栅格地图文件读取器
// ============================================================================
class GridMapReader {
public:
    // 从二进制文件读取栅格地图
    bool ReadFromFile(const std::string& filepath, GridMapFrame& frame);
    
    // 生成测试栅格地图
    void GenerateTestMap(GridMapFrame& frame, uint32_t width = 100, uint32_t height = 100);
};

// ============================================================================
// 栅格地图发送器
// ============================================================================
class GridMapTransmitter {
public:
    GridMapTransmitter(std::shared_ptr<UnifiedSender> unified_sender);
    ~GridMapTransmitter();
    
    // 从文件发送栅格地图
    bool SendFromFile(const std::string& filepath);
    
    // 发送测试栅格地图
    void SendTestMap(uint32_t width = 100, uint32_t height = 100);
    
    // 停止发送
    void Stop();

private:
    // 发送单帧（支持分块）
    bool SendFrame(const GridMapFrame& frame, uint32_t frame_seq);
    
    std::shared_ptr<UnifiedSender> unified_sender_;
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> frame_counter_{0};
    std::mutex send_mutex_;
};

// ============================================================================
// 栅格地图接收器
// ============================================================================
class GridMapReceiver : public Receiver::Visitor {
public:
    GridMapReceiver(int listen_port);
    ~GridMapReceiver();
    
    // 启动接收
    void Start();
    
    // 停止接收
    void Stop();
    
    // 设置输出文件
    void SetOutputFile(const std::string& filepath);
    
    // 获取统计
    struct Stats {
        uint32_t frames_received = 0;
        uint32_t cells_received = 0;
    };
    Stats GetStats() const { return stats_; }

private:
    // Receiver::Visitor 回调
    void OnDecodeComplete(uint32_t stream_id, const std::vector<uint8_t>& data) override;
    
    // 保存栅格地图到文件
    void SaveGridMap(const GridMapFrame& frame);
    
    std::unique_ptr<Receiver> receiver_;
    std::atomic<bool> running_{false};
    std::string output_path_;
    std::ofstream output_file_;
    std::mutex file_mutex_;
    
    Stats stats_;
};

} // namespace DataTransmit

#endif // GRID_MAP_H
