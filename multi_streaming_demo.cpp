/**
 * multi_streaming_demo.cpp - 统一多数据流传输演示程序
 * 
 * 支持四种数据类型：
 * - video (9001): H.264视频流传输
 * - fc (9000): 飞控指令实时传输（交互式）
 * - pointcloud (9002): 点云数据传输
 * - gridmap (9003): 栅格地图传输
 * 
 * 使用方法:
 *   # 视频传输
 *   ./multi_streaming_demo receiver video 9001 output.mp4
 *   ./multi_streaming_demo sender video 127.0.0.1 9001 input.mp4
 * 
 *   # 飞控指令（交互式）
 *   ./multi_streaming_demo receiver fc 9000
 *   ./multi_streaming_demo sender fc 127.0.0.1 9000
 * 
 *   # 点云传输
 *   ./multi_streaming_demo receiver pointcloud 9002 output.pcd
 *   ./multi_streaming_demo sender pointcloud 127.0.0.1 9002 input.pcd
 * 
 *   # 栅格地图传输
 *   ./multi_streaming_demo receiver gridmap 9003 output.grid
 *   ./multi_streaming_demo sender gridmap 127.0.0.1 9003 input.grid
 */

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include "data_common.h"
#include "video_transmitter.h"
#include "video_receiver.h"
#include "fc_control.h"
#include "point_cloud.h"
#include "grid_map.h"
#include "unified_sender.h"

using namespace DataTransmit;
using VideoTransmit::VideoTransmitter;
using VideoTransmit::VideoReceiver;

static std::atomic<bool> g_running(true);

// ============================================================================
// 信号处理
// ============================================================================
void SignalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

// ============================================================================
// 打印用法
// ============================================================================
void PrintUsage(const char* program) {
    std::cout << "用法:" << std::endl;
    std::cout << "  " << program << " <receiver|sender> <type> [参数...]" << std::endl;
    std::cout << std::endl;
    std::cout << "数据类型:" << std::endl;
    std::cout << "  video      - 视频流 (端口9001)" << std::endl;
    std::cout << "  fc         - 飞控指令 (端口9000)" << std::endl;
    std::cout << "  pointcloud - 点云数据 (端口9002)" << std::endl;
    std::cout << "  gridmap    - 栅格地图 (端口9003)" << std::endl;
    std::cout << std::endl;
    std::cout << "接收端命令:" << std::endl;
    std::cout << "  " << program << " receiver video <port> <output.mp4>" << std::endl;
    std::cout << "  " << program << " receiver fc <port> [log.txt]" << std::endl;
    std::cout << "  " << program << " receiver pointcloud <port> [output.pcd]" << std::endl;
    std::cout << "  " << program << " receiver gridmap <port> [output.grid]" << std::endl;
    std::cout << std::endl;
    std::cout << "发送端命令:" << std::endl;
    std::cout << "  " << program << " sender video <addr> <port> <input.mp4>" << std::endl;
    std::cout << "  " << program << " sender fc <addr> <port>" << std::endl;
    std::cout << "  " << program << " sender pointcloud <addr> <port> <input.pcd>" << std::endl;
    std::cout << "  " << program << " sender gridmap <addr> <port> <input.grid>" << std::endl;
    std::cout << std::endl;
    std::cout << "说明:" << std::endl;
    std::cout << "  - 输入文件自动从 data/<type>/ 目录读取" << std::endl;
    std::cout << "  - 输出文件自动保存到 output/<type>/ 目录" << std::endl;
    std::cout << "  - 飞控指令: 输入 '!high ', '!normal ', '!low ' 设置优先级" << std::endl;
}

// ============================================================================
// 视频传输
// ============================================================================
int RunVideoReceiver(int port, const std::string& output_file) {
    std::string output_path = GetOutputDir(DataTransmit::DataType::VIDEO) + output_file;
    
    VideoReceiver receiver(port);
    receiver.CreateOutputFile(output_path);
    receiver.Start();
    
    // 等待配置
    std::cout << "等待视频配置..." << std::endl;
    while (!receiver.IsOutputOpen() && g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (!g_running) {
        receiver.Stop();
        return 0;
    }
    
    std::cout << "开始接收视频数据... (按 Ctrl+C 停止)" << std::endl;
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    receiver.CloseOutputFile();
    std::cout << "视频已保存到: " << output_path << std::endl;
    return 0;
}

int RunVideoSender(const std::string& addr, int port, const std::string& input_file) {
    std::string input_path = GetInputDir(DataTransmit::DataType::VIDEO) + input_file;
    
    // 创建 UnifiedSender 配置
    UnifiedSenderConfig config;
    config.target_ip = addr;
    
    // 创建 UnifiedSender
    auto unified_sender = std::make_shared<UnifiedSender>(config);
    if (!unified_sender->initialize()) {
        std::cerr << "Failed to initialize UnifiedSender" << std::endl;
        return 1;
    }
    unified_sender->start();
    
    VideoTransmitter transmitter(unified_sender);
    transmitter.OpenVideoFile(input_path);
    transmitter.Start();
    
    std::cout << "开始发送视频: " << input_path << std::endl;
    std::cout << "(按 Ctrl+C 停止)" << std::endl;
    
    while (g_running && transmitter.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    transmitter.Stop();
    unified_sender->stop();
    std::cout << "视频发送完成" << std::endl;
    return 0;
}

// ============================================================================
// 飞控指令传输
// ============================================================================
int RunFCReceiver(int port, const std::string& log_file) {
    FCControlReceiver receiver(port);
    
    if (!log_file.empty()) {
        std::string log_path = GetOutputDir(DataTransmit::DataType::FC_CONTROL) + log_file;
        receiver.SetLogFile(log_path);
    }
    
    receiver.Start();
    
    // 飞控接收端保持运行直到Ctrl+C
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    return 0;
}

int RunFCSender(const std::string& addr, int port) {
    // 创建 UnifiedSender 配置
    UnifiedSenderConfig config;
    config.target_ip = addr;
    
    // 创建 UnifiedSender
    auto unified_sender = std::make_shared<UnifiedSender>(config);
    if (!unified_sender->initialize()) {
        std::cerr << "Failed to initialize UnifiedSender" << std::endl;
        return 1;
    }
    unified_sender->start();
    
    FCControlTransmitter transmitter(unified_sender);
    
    // 设置信号处理，用于优雅退出输入循环
    signal(SIGINT, SignalHandler);
    
    transmitter.Run();
    
    unified_sender->stop();
    return 0;
}

// ============================================================================
// 点云传输
// ============================================================================
int RunPointCloudReceiver(int port, const std::string& output_file) {
    PointCloudReceiver receiver(port);
    
    if (!output_file.empty()) {
        std::string output_path = GetOutputDir(DataTransmit::DataType::POINT_CLOUD) + output_file;
        receiver.SetOutputFile(output_path);
    }
    
    receiver.Start();
    
    std::cout << "等待接收点云数据... (按 Ctrl+C 停止)" << std::endl;
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    
    auto stats = receiver.GetStats();
    std::cout << "接收统计: " << stats.frames_received << " 帧, " 
              << stats.points_received << " 点" << std::endl;
    return 0;
}

int RunPointCloudSender(const std::string& addr, int port, const std::string& input_file) {
    // 创建 UnifiedSender 配置
    UnifiedSenderConfig config;
    config.target_ip = addr;
    
    // 创建 UnifiedSender
    auto unified_sender = std::make_shared<UnifiedSender>(config);
    if (!unified_sender->initialize()) {
        std::cerr << "Failed to initialize UnifiedSender" << std::endl;
        return 1;
    }
    unified_sender->start();
    
    PointCloudTransmitter transmitter(unified_sender);
    
    signal(SIGINT, SignalHandler);
    
    if (!input_file.empty() && input_file != "test" && input_file.find(':') == std::string::npos) {
        std::string input_path = GetInputDir(DataTransmit::DataType::POINT_CLOUD) + input_file;
        transmitter.SendFromFile(input_path);
    } else {
        // 发送测试点云
        uint32_t point_count = 1000;
        uint32_t frame_count = 10;
        if (!input_file.empty()) {
            // 解析参数如 "1000:10" (1000点/帧，10帧)
            size_t pos = input_file.find(':');
            if (pos != std::string::npos) {
                point_count = std::stoul(input_file.substr(0, pos));
                frame_count = std::stoul(input_file.substr(pos + 1));
            }
        }
        transmitter.SendTestCloud(point_count, frame_count);
    }
    
    unified_sender->stop();
    return 0;
}

// ============================================================================
// 栅格地图传输
// ============================================================================
int RunGridMapReceiver(int port, const std::string& output_file) {
    GridMapReceiver receiver(port);
    
    if (!output_file.empty()) {
        std::string output_path = GetOutputDir(DataTransmit::DataType::GRID_MAP) + output_file;
        receiver.SetOutputFile(output_path);
    }
    
    receiver.Start();
    
    std::cout << "等待接收栅格地图数据... (按 Ctrl+C 停止)" << std::endl;
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    
    auto stats = receiver.GetStats();
    std::cout << "接收统计: " << stats.frames_received << " 帧, " 
              << stats.cells_received << " 单元格" << std::endl;
    return 0;
}

int RunGridMapSender(const std::string& addr, int port, const std::string& input_file) {
    // 创建 UnifiedSender 配置
    UnifiedSenderConfig config;
    config.target_ip = addr;
    
    // 创建 UnifiedSender
    auto unified_sender = std::make_shared<UnifiedSender>(config);
    if (!unified_sender->initialize()) {
        std::cerr << "Failed to initialize UnifiedSender" << std::endl;
        return 1;
    }
    unified_sender->start();
    
    GridMapTransmitter transmitter(unified_sender);
    
    signal(SIGINT, SignalHandler);
    
    if (!input_file.empty() && input_file != "test") {
        std::string input_path = GetInputDir(DataTransmit::DataType::GRID_MAP) + input_file;
        transmitter.SendFromFile(input_path);
    } else {
        // 发送测试栅格地图
        transmitter.SendTestMap(100, 100);
    }
    
    unified_sender->stop();
    return 0;
}

// ============================================================================
// 主函数
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 4) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string role = argv[1];
    std::string type = argv[2];
    
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    
    // 根据角色和数据类型路由
    if (role == "receiver") {
        int port = std::stoi(argv[3]);
        std::string output_file = (argc > 4) ? argv[4] : "";
        
        if (type == "video") {
            if (argc < 5) {
                std::cerr << "视频接收需要输出文件名" << std::endl;
                return 1;
            }
            return RunVideoReceiver(port, argv[4]);
        } else if (type == "fc") {
            return RunFCReceiver(port, output_file);
        } else if (type == "pointcloud") {
            return RunPointCloudReceiver(port, output_file);
        } else if (type == "gridmap") {
            return RunGridMapReceiver(port, output_file);
        } else {
            std::cerr << "未知数据类型: " << type << std::endl;
            return 1;
        }
        
    } else if (role == "sender") {
        if (argc < 5) {
            PrintUsage(argv[0]);
            return 1;
        }
        
        std::string addr = argv[3];
        int port = std::stoi(argv[4]);
        std::string input_file = (argc > 5) ? argv[5] : "";
        
        if (type == "video") {
            if (argc < 6) {
                std::cerr << "视频发送需要输入文件名" << std::endl;
                return 1;
            }
            return RunVideoSender(addr, port, argv[5]);
        } else if (type == "fc") {
            return RunFCSender(addr, port);
        } else if (type == "pointcloud") {
            return RunPointCloudSender(addr, port, input_file);
        } else if (type == "gridmap") {
            return RunGridMapSender(addr, port, input_file);
        } else {
            std::cerr << "未知数据类型: " << type << std::endl;
            return 1;
        }
        
    } else {
        std::cerr << "未知角色: " << role << " (应为 receiver 或 sender)" << std::endl;
        return 1;
    }
    
    return 0;
}
