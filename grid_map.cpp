/**
 * grid_map.cpp - 栅格地图传输实现
 */

#include "grid_map.h"
#include "pack/rq_pack.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <random>
#include <thread>
#include <ctime>
#include <chrono>

namespace DataTransmit {

// ============================================================================
// GridMapReader 实现
// ============================================================================

bool GridMapReader::ReadFromFile(const std::string& filepath, GridMapFrame& frame) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "无法打开栅格地图文件: " << filepath << std::endl;
        return false;
    }
    
    // 读取头部
    file.read(reinterpret_cast<char*>(&frame.header), sizeof(GridMapHeader));
    
    // 读取数据
    size_t data_size = frame.header.width * frame.header.height;
    frame.data.resize(data_size);
    file.read(reinterpret_cast<char*>(frame.data.data()), data_size);
    
    std::cout << "读取栅格地图: " << frame.header.width << "x" << frame.header.height
              << " (" << data_size << " 单元格)" << std::endl;
    return true;
}

void GridMapReader::GenerateTestMap(GridMapFrame& frame, uint32_t width, uint32_t height) {
    frame.header.timestamp = GetCurrentTimestampUs();
    frame.header.seq = 0;
    frame.header.width = width;
    frame.header.height = height;
    frame.header.resolution = 0.1;  // 0.1米/单元格
    frame.header.origin_x = 0.0;
    frame.header.origin_y = 0.0;
    frame.header.origin_z = 0.0;
    
    size_t data_size = width * height;
    frame.data.resize(data_size);
    
    // 生成随机障碍物地图
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 99);
    
    for (size_t i = 0; i < data_size; ++i) {
        int val = dist(gen);
        if (val < 70) {
            frame.data[i] = static_cast<uint8_t>(GridCellValue::FREE);      // 70% 空闲
        } else if (val < 90) {
            frame.data[i] = static_cast<uint8_t>(GridCellValue::OCCUPIED); // 20% 占据
        } else {
            frame.data[i] = static_cast<uint8_t>(GridCellValue::UNKNOWN);  // 10% 未知
        }
    }
}

// ============================================================================
// GridMapTransmitter 实现
// ============================================================================

GridMapTransmitter::GridMapTransmitter(std::shared_ptr<UnifiedSender> unified_sender)
    : unified_sender_(unified_sender), running_(true) {
}

GridMapTransmitter::~GridMapTransmitter() {
    Stop();
}

bool GridMapTransmitter::SendFromFile(const std::string& filepath) {
    GridMapFrame frame;
    GridMapReader reader;
    
    if (!reader.ReadFromFile(filepath, frame)) {
        return false;
    }
    
    std::cout << "开始发送栅格地图..." << std::endl;
    
    bool result = SendFrame(frame, 0);
    
    std::cout << "栅格地图发送完成" << std::endl;
    return result;
}

void GridMapTransmitter::SendTestMap(uint32_t width, uint32_t height) {
    GridMapFrame frame;
    GridMapReader reader;
    reader.GenerateTestMap(frame, width, height);
    
    std::cout << "发送测试栅格地图: " << width << "x" << height << std::endl;
    
    // 模拟多次更新
    for (int update = 0; update < 5 && running_; ++update) {
        frame.header.seq = update;
        frame.header.timestamp = GetCurrentTimestampUs();
        
        // 随机更新一些单元格
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> pos_dist(0, frame.data.size() - 1);
        std::uniform_int_distribution<int> val_dist(0, 2);
        
        for (int i = 0; i < 100; ++i) {
            size_t pos = pos_dist(gen);
            frame.data[pos] = static_cast<uint8_t>(val_dist(gen));
        }
        
        if (SendFrame(frame, update)) {
            std::cout << "发送栅格更新 #" << update << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::cout << "栅格地图发送完成" << std::endl;
}

void GridMapTransmitter::Stop() {
    // UnifiedSender lifecycle is managed externally
}

bool GridMapTransmitter::SendFrame(const GridMapFrame& frame, uint32_t frame_seq) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    
    // 计算序列化大小
    size_t data_size = sizeof(GridMapHeader) + frame.data.size();
    
    // 序列化
    std::vector<uint8_t> data(data_size);
    memcpy(data.data(), &frame.header, sizeof(GridMapHeader));
    memcpy(data.data() + sizeof(GridMapHeader), frame.data.data(), frame.data.size());
    
    uint32_t stream_id = frame_seq + 1;
    return unified_sender_->send(DataPriority::GRID_MAP, stream_id, data);
}

// ============================================================================
// GridMapReceiver 实现
// ============================================================================

GridMapReceiver::GridMapReceiver(std::shared_ptr<DataTransmit::UnifiedReceiver> unified_receiver)
    : unified_receiver_(unified_receiver) {
}

GridMapReceiver::~GridMapReceiver() {
    Stop();
}

void GridMapReceiver::Start() {
    running_ = true;
    
    // 设置解码回调（使用多回调注册API）
    callback_id_ = unified_receiver_->registerDecodeCallback([this](DataPriority priority, uint32_t stream_id,
                                                 const std::vector<uint8_t>& data) {
        if (priority == DataPriority::GRID_MAP) {
            OnFrameReceived(priority, stream_id, data);
        }
    });
    
    std::cout << "========================================" << std::endl;
    std::cout << "   栅格地图接收端" << std::endl;
    std::cout << "========================================" << std::endl;
}

void GridMapReceiver::Stop() {
    running_ = false;
    
    // 注销回调
    if (callback_id_ >= 0) {
        unified_receiver_->unregisterDecodeCallback(callback_id_);
        callback_id_ = -1;
    }
    
    if (output_file_.is_open()) {
        output_file_.close();
    }
}

void GridMapReceiver::SetOutputFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    output_path_ = filepath;
}

void GridMapReceiver::OnFrameReceived(DataPriority priority, uint32_t stream_id,
                                      const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(GridMapHeader)) {
        return;
    }
    
    // 解析头部
    GridMapFrame frame;
    memcpy(&frame.header, data.data(), sizeof(GridMapHeader));
    
    // 解析数据
    size_t expected_size = frame.header.width * frame.header.height;
    size_t actual_size = data.size() - sizeof(GridMapHeader);
    
    if (actual_size < expected_size) {
        std::cerr << "栅格地图数据大小不匹配: 期望=" << expected_size 
                  << " (" << frame.header.width << "x" << frame.header.height 
                  << "), 实际=" << actual_size 
                  << ", 数据总大小=" << data.size() 
                  << ", 头大小=" << sizeof(GridMapHeader) << std::endl;
        return;
    }
    
    frame.data.resize(expected_size);
    memcpy(frame.data.data(), data.data() + sizeof(GridMapHeader), expected_size);
    
    stats_.frames_received++;
    stats_.cells_received += expected_size;
    
    // 统计各类单元格数量
    size_t free_count = 0, occupied_count = 0, unknown_count = 0;
    for (auto cell : frame.data) {
        switch (static_cast<GridCellValue>(cell)) {
            case GridCellValue::FREE: free_count++; break;
            case GridCellValue::OCCUPIED: occupied_count++; break;
            case GridCellValue::UNKNOWN: unknown_count++; break;
        }
    }
    
    std::cout << "[栅格] 接收帧 " << frame.header.seq << ": " 
              << frame.header.width << "x" << frame.header.height
              << " (空闲:" << free_count << " 占据:" << occupied_count 
              << " 未知:" << unknown_count << ")" << std::endl;
    
    // 保存栅格地图
    SaveGridMap(frame);
}

void GridMapReceiver::SaveGridMap(const GridMapFrame& frame) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    std::string filepath = output_path_;
    if (filepath.empty()) {
        // 自动生成文件名
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << "output/gridmap/received_" 
           << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") 
           << "_" << frame.header.seq << ".grid";
        filepath = ss.str();
    }
    
    // 以二进制格式保存
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "无法创建栅格地图文件: " << filepath << std::endl;
        return;
    }
    
    // 写入头部
    file.write(reinterpret_cast<const char*>(&frame.header), sizeof(GridMapHeader));
    
    // 写入数据
    file.write(reinterpret_cast<const char*>(frame.data.data()), frame.data.size());
    
    file.close();
    std::cout << "[栅格] 保存到 " << filepath << std::endl;
}

} // namespace DataTransmit
