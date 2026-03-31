/**
 * point_cloud.cpp - 点云传输实现
 */

#include "point_cloud.h"
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
// PointCloudReader 实现
// ============================================================================

bool PointCloudReader::ReadFromPCD(const std::string& filepath, 
                                   std::vector<PointCloudPoint>& points) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "无法打开点云文件: " << filepath << std::endl;
        return false;
    }
    
    points.clear();
    std::string line;
    bool header_done = false;
    uint32_t point_count = 0;
    
    // 简单PCD解析（假设ASCII格式，x y z字段）
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        if (!header_done) {
            if (line.substr(0, 7) == "POINTS ") {
                point_count = std::stoul(line.substr(7));
            } else if (line == "DATA ascii") {
                header_done = true;
            }
            continue;
        }
        
        // 解析点数据
        std::istringstream iss(line);
        float x, y, z;
        if (iss >> x >> y >> z) {
            PointCloudPoint pt;
            pt.x = x;
            pt.y = y;
            pt.z = z;
            pt.offset_time = static_cast<uint32_t>(points.size() * 1000);  // 模拟时间偏移
            pt.reflectivity = 100;
            pt.tag = 0;
            pt.line = static_cast<uint8_t>(points.size() % 4);
            pt.reserved = 0;
            points.push_back(pt);
        }
    }
    
    std::cout << "读取点云文件: " << points.size() << " 点" << std::endl;
    return true;
}

void PointCloudReader::GenerateTestCloud(std::vector<PointCloudPoint>& points, 
                                         uint32_t count) {
    points.clear();
    points.reserve(count);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
    std::uniform_int_distribution<int> reflect_dist(0, 255);
    
    for (uint32_t i = 0; i < count; ++i) {
        PointCloudPoint pt;
        pt.x = dist(gen);
        pt.y = dist(gen);
        pt.z = dist(gen);
        pt.offset_time = i * 1000;  // 纳秒
        pt.reflectivity = static_cast<uint8_t>(reflect_dist(gen));
        pt.tag = static_cast<uint8_t>(i % 4);
        pt.line = static_cast<uint8_t>(i % 16);
        pt.reserved = 0;
        points.push_back(pt);
    }
}

// ============================================================================
// PointCloudTransmitter 实现
// ============================================================================

PointCloudTransmitter::PointCloudTransmitter(std::shared_ptr<UnifiedSender> unified_sender)
    : unified_sender_(unified_sender), running_(true) {
}

PointCloudTransmitter::~PointCloudTransmitter() {
    Stop();
}

bool PointCloudTransmitter::SendFromFile(const std::string& filepath) {
    std::vector<PointCloudPoint> points;
    PointCloudReader reader;
    
    if (!reader.ReadFromPCD(filepath, points)) {
        return false;
    }
    
    // 将点云分为多帧发送（每帧最多100个点，模拟100ms周期）
    const uint32_t points_per_frame = 100;
    uint32_t total_frames = (points.size() + points_per_frame - 1) / points_per_frame;
    
    running_ = true;
    
    std::cout << "开始发送点云: " << points.size() << " 点, " 
              << total_frames << " 帧" << std::endl;
    
    for (uint32_t frame_idx = 0; frame_idx < total_frames && running_; ++frame_idx) {
        uint32_t start_idx = frame_idx * points_per_frame;
        uint32_t end_idx = std::min(start_idx + points_per_frame, 
                                    static_cast<uint32_t>(points.size()));
        
        PointCloudFrame frame;
        frame.header.timestamp = GetCurrentTimestampUs();
        frame.header.timebase = frame.header.timestamp;
        frame.header.point_num = end_idx - start_idx;
        frame.header.seq = frame_idx;
        frame.header.lidar_id = 1;
        frame.points.assign(points.begin() + start_idx, points.begin() + end_idx);
        
        if (SendFrame(frame, frame_idx)) {
            std::cout << "发送帧 " << frame_idx << ": " << frame.header.point_num 
                      << " 点" << std::endl;
        }
        
        // 模拟100ms发送间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "点云发送完成" << std::endl;
    return true;
}

void PointCloudTransmitter::SendTestCloud(uint32_t point_count, uint32_t frame_count) {
    running_ = true;
    
    std::cout << "发送测试点云: " << point_count << " 点/帧, " 
              << frame_count << " 帧" << std::endl;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);
    
    for (uint32_t frame_idx = 0; frame_idx < frame_count && running_; ++frame_idx) {
        PointCloudFrame frame;
        frame.header.timestamp = GetCurrentTimestampUs();
        frame.header.timebase = frame.header.timestamp;
        frame.header.point_num = point_count;
        frame.header.seq = frame_idx;
        frame.header.lidar_id = 1;
        
        frame.points.reserve(point_count);
        for (uint32_t i = 0; i < point_count; ++i) {
            PointCloudPoint pt;
            pt.x = dist(gen);
            pt.y = dist(gen);
            pt.z = dist(gen);
            pt.offset_time = i * 100;
            pt.reflectivity = 100;
            pt.tag = 0;
            pt.line = i % 4;
            pt.reserved = 0;
            frame.points.push_back(pt);
        }
        
        if (SendFrame(frame, frame_idx)) {
            std::cout << "发送帧 " << frame_idx << "/" << frame_count << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "测试点云发送完成" << std::endl;
}

void PointCloudTransmitter::Stop() {
    running_ = false;
}

bool PointCloudTransmitter::SendFrame(const PointCloudFrame& frame, uint32_t frame_seq) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    
    // 计算序列化大小
    size_t data_size = sizeof(PointCloudHeader) + 
                       frame.points.size() * sizeof(PointCloudPoint);
    
    // 序列化
    std::vector<uint8_t> data(data_size);
    memcpy(data.data(), &frame.header, sizeof(PointCloudHeader));
    memcpy(data.data() + sizeof(PointCloudHeader), 
           frame.points.data(), 
           frame.points.size() * sizeof(PointCloudPoint));
    
    uint32_t stream_id = frame_seq + 1;
    return unified_sender_->send(DataPriority::POINT_CLOUD, stream_id, data);
}

// ============================================================================
// PointCloudReceiver 实现
// ============================================================================

PointCloudReceiver::PointCloudReceiver(int listen_port)
    : receiver_(std::make_unique<Receiver>(this, listen_port, 2)) {
}

PointCloudReceiver::~PointCloudReceiver() {
    Stop();
}

void PointCloudReceiver::Start() {
    running_ = true;
    
    // 在单独线程中启动接收器（因为start()是阻塞的）
    std::thread receiver_thread([this]() {
        receiver_->start();
    });
    receiver_thread.detach();
    
    std::cout << "========================================" << std::endl;
    std::cout << "   点云接收端" << std::endl;
    std::cout << "========================================" << std::endl;
}

void PointCloudReceiver::Stop() {
    running_ = false;
    
    // 保存剩余点云
    if (!accumulated_points_.empty()) {
        SavePointCloud(accumulated_points_);
    }
    
    receiver_->stop();
    if (output_file_.is_open()) {
        output_file_.close();
    }
}

void PointCloudReceiver::SetOutputFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    output_path_ = filepath;
    if (output_file_.is_open()) {
        output_file_.close();
    }
}

void PointCloudReceiver::OnDecodeComplete(uint32_t stream_id,
                                         const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(PointCloudHeader)) {
        return;
    }
    
    // 解析头部
    PointCloudHeader header;
    memcpy(&header, data.data(), sizeof(PointCloudHeader));
    
    // 解析点数据
    size_t expected_points_size = header.point_num * sizeof(PointCloudPoint);
    size_t actual_points_size = data.size() - sizeof(PointCloudHeader);
    
    if (actual_points_size < expected_points_size) {
        std::cerr << "点云数据大小不匹配" << std::endl;
        return;
    }
    
    std::vector<PointCloudPoint> points(header.point_num);
    memcpy(points.data(), data.data() + sizeof(PointCloudHeader), 
           header.point_num * sizeof(PointCloudPoint));
    
    stats_.frames_received++;
    stats_.points_received += header.point_num;
    
    std::cout << "[点云] 接收帧 " << header.seq << ": " << header.point_num 
              << " 点 (总计: " << stats_.points_received << ")" << std::endl;
    
    // 累积点云
    {
        std::lock_guard<std::mutex> lock(points_mutex_);
        accumulated_points_.insert(accumulated_points_.end(), 
                                   points.begin(), points.end());
    }
    
    // 每10帧保存一次
    if (stats_.frames_received % 10 == 0) {
        std::vector<PointCloudPoint> points_to_save;
        {
            std::lock_guard<std::mutex> lock(points_mutex_);
            points_to_save = accumulated_points_;
            accumulated_points_.clear();
        }
        SavePointCloud(points_to_save);
    }
}

void PointCloudReceiver::SavePointCloud(const std::vector<PointCloudPoint>& points) {
    if (points.empty()) return;
    
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    std::string filepath = output_path_;
    if (filepath.empty()) {
        // 自动生成文件名
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << "output/pointcloud/received_" 
           << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".pcd";
        filepath = ss.str();
        output_path_ = filepath;
    }
    
    // 追加模式
    bool file_exists = std::ifstream(filepath).good();
    
    if (!output_file_.is_open()) {
        output_file_.open(filepath, std::ios::out | std::ios::app);
    }
    
    // 如果是新文件，写入PCD头部
    if (!file_exists) {
        output_file_ << "# .PCD v0.7 - Point Cloud Data file format" << std::endl;
        output_file_ << "VERSION 0.7" << std::endl;
        output_file_ << "FIELDS x y z intensity" << std::endl;
        output_file_ << "SIZE 4 4 4 1" << std::endl;
        output_file_ << "TYPE F F F U" << std::endl;
        output_file_ << "COUNT 1 1 1 1" << std::endl;
        output_file_ << "WIDTH " << points.size() << std::endl;
        output_file_ << "HEIGHT 1" << std::endl;
        output_file_ << "VIEWPOINT 0 0 0 1 0 0 0" << std::endl;
        output_file_ << "POINTS " << points.size() << std::endl;
        output_file_ << "DATA ascii" << std::endl;
    }
    
    // 写入点数据
    for (const auto& pt : points) {
        output_file_ << pt.x << " " << pt.y << " " << pt.z 
                     << " " << (int)pt.reflectivity << std::endl;
    }
    
    output_file_.flush();
    std::cout << "[点云] 保存 " << points.size() << " 点到 " << filepath << std::endl;
}

} // namespace DataTransmit
