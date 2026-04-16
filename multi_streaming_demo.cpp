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
#include "voice_transmitter.h"
#include "voice_receiver.h"
#include "fc_control.h"
#include "point_cloud.h"
#include "grid_map.h"
#include "unified_sender.h"
#include "unified_receiver.h"

using namespace DataTransmit;
using VideoTransmit::VideoTransmitter;
using VideoTransmit::VideoReceiver;
using VoiceReceive::VoiceReceiver;
using VoiceTransmit::VoiceTransmitter;

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
    std::cout << "  voice      - 语音流 (端口9004)" << std::endl;
    std::cout << "  fc         - 飞控指令 (端口9000)" << std::endl;
    std::cout << "  pointcloud - 点云数据 (端口9002)" << std::endl;
    std::cout << "  gridmap    - 栅格地图 (端口9003)" << std::endl;
    std::cout << std::endl;
    std::cout << "接收端命令:" << std::endl;
    std::cout << "  " << program << " receiver video <port> <output.mp4>" << std::endl;
    std::cout << "  " << program << " receiver voice <port> <output.wav>" << std::endl;
    std::cout << "  " << program << " receiver fc <port> [log.txt]" << std::endl;
    std::cout << "  " << program << " receiver pointcloud <port> [output.pcd]" << std::endl;
    std::cout << "  " << program << " receiver gridmap <port> [output.grid]" << std::endl;
    std::cout << std::endl;
    std::cout << "发送端命令:" << std::endl;
    std::cout << "  " << program << " sender video <addr> <port> <input.mp4>" << std::endl;
    std::cout << "  " << program << " sender voice <addr> <port> <input.wav>" << std::endl;
    std::cout << "  " << program << " sender fc <addr> <port>" << std::endl;
    std::cout << "  " << program << " sender pointcloud <addr> <port> <input.pcd>" << std::endl;
    std::cout << "  " << program << " sender gridmap <addr> <port> <input.grid>" << std::endl;
    std::cout << std::endl;
    std::cout << "统一接收（同时接收所有类型，共享同一个UnifiedReceiver）:" << std::endl;
    std::cout << "  " << program << " receiver_all <base_port>" << std::endl;
    std::cout << std::endl;
    std::cout << "说明:" << std::endl;
    std::cout << "  - 输入文件自动从 data/<type>/ 目录读取" << std::endl;
    std::cout << "  - 输出文件自动保存到 output/<type>/ 目录" << std::endl;
    std::cout << "  - 飞控指令: 输入 '!high ', '!normal ', '!low ' 设置优先级" << std::endl;
    std::cout << "  - receiver_all 模式可以同时接收所有5种数据类型" << std::endl;
}

// ============================================================================
// 统一接收（同时接收所有数据类型）
// ============================================================================
int RunUnifiedReceiver(int base_port) {
    std::cout << "========================================" << std::endl;
    std::cout << "   统一接收端（所有数据类型）" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "基端口: " << base_port << " (监听 " << base_port << "-" << (base_port+4) << ")" << std::endl;
    std::cout << "按 Ctrl+C 停止" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 创建统一的 UnifiedReceiver（所有接收器共享）
    auto unified_receiver = std::make_shared<UnifiedReceiver>(base_port);
    
    // 创建5种专用接收器（共享同一个 UnifiedReceiver）
    FCControlReceiver fc_receiver(unified_receiver);
    VoiceReceiver voice_receiver(unified_receiver);
    VideoReceiver video_receiver(unified_receiver);
    PointCloudReceiver pointcloud_receiver(unified_receiver);
    GridMapReceiver gridmap_receiver(unified_receiver);
    
    // 设置输出（如有需要）
    std::string video_output = GetOutputDir(DataTransmit::DataType::VIDEO) + "received.mp4";
    std::string voice_output = "output/voice/received.wav";
    std::string pc_output = GetOutputDir(DataTransmit::DataType::POINT_CLOUD) + "received.pcd";
    std::string gm_output = GetOutputDir(DataTransmit::DataType::GRID_MAP) + "received.grid";
    
    // 创建输出目录
    system("mkdir -p output/videos output/voice output/pointclouds output/gridmaps");
    
    // 先启动各专用接收器设置回调（必须在UnifiedReceiver启动前，否则会丢失初始配置包）
    fc_receiver.Start();
    voice_receiver.Start();
    voice_receiver.CreateOutput(voice_output);
    video_receiver.Start();
    pointcloud_receiver.Start();
    pointcloud_receiver.SetOutputFile(pc_output);
    gridmap_receiver.Start();
    gridmap_receiver.SetOutputFile(gm_output);
    
    // 启动 UnifiedReceiver（开始接收数据）
    unified_receiver->start();
    
    // 等待视频配置并创建输出文件
    std::cout << "等待视频配置..." << std::endl;
    int wait_count = 0;
    while (!video_receiver.IsOutputOpen() && g_running && wait_count < 300) {
        if (!video_receiver.CreateOutputFile(video_output)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            wait_count++;
            if (wait_count % 10 == 0) {
                std::cout << "  等待视频配置... (" << wait_count * 100 << "ms)" << std::endl;
            }
        }
    }
    
    if (!video_receiver.IsOutputOpen()) {
        std::cerr << "警告: 未能创建视频输出文件" << std::endl;
    }
    
    std::cout << "所有接收器已启动，等待数据..." << std::endl;
    
    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 停止所有接收器
    fc_receiver.Stop();
    voice_receiver.Stop();
    video_receiver.Stop();
    pointcloud_receiver.Stop();
    gridmap_receiver.Stop();
    unified_receiver->stop();
    
    // 关闭输出文件（确保MP4文件尾写入）
    video_receiver.CloseOutputFile();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "接收统计:" << std::endl;
    std::cout << "  Voice: " << voice_receiver.GetStats().frames_received << " 帧" << std::endl;
    std::cout << "  Video: 见 " << video_output << std::endl;
    std::cout << "  PointCloud: " << pointcloud_receiver.GetStats().frames_received << " 帧, " 
              << pointcloud_receiver.GetStats().points_received << " 点" << std::endl;
    std::cout << "  GridMap: " << gridmap_receiver.GetStats().frames_received << " 帧, "
              << gridmap_receiver.GetStats().cells_received << " 单元格" << std::endl;
    
    return 0;
}

// ============================================================================
// 视频传输
// ============================================================================
int RunVideoReceiver(int port, const std::string& output_file) {
    std::string output_path = GetOutputDir(DataTransmit::DataType::VIDEO) + output_file;
    
    // 确保输出目录存在
    std::string cmd = "mkdir -p " + GetOutputDir(DataTransmit::DataType::VIDEO);
    system(cmd.c_str());
    
    auto unified_receiver = std::make_shared<UnifiedReceiver>(port);
    
    VideoReceiver receiver(unified_receiver);
    receiver.Start();  // 先设置回调，再启动UnifiedReceiver
    
    unified_receiver->start();
    
    // 等待并创建输出文件
    std::cout << "等待视频配置..." << std::endl;
    int wait_count = 0;
    while (!receiver.IsOutputOpen() && g_running && wait_count < 300) {
        if (!receiver.CreateOutputFile(output_path)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            wait_count++;
            if (wait_count % 10 == 0) {
                std::cout << "  等待配置... (" << wait_count * 100 << "ms)" << std::endl;
            }
        }
    }
    
    if (!receiver.IsOutputOpen()) {
        std::cerr << "错误: 未能创建输出文件或接收配置" << std::endl;
        receiver.Stop();
        unified_receiver->stop();
        return 1;
    }
    
    std::cout << "输出文件已创建: " << output_path << std::endl;
    std::cout << "开始接收视频数据... (按 Ctrl+C 停止)" << std::endl;
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    unified_receiver->stop();
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
// 语音传输
// ============================================================================
int RunVoiceReceiver(int port, const std::string& output_file) {
    std::string output_path = "output/voice/" + output_file;
    
    // 确保输出目录存在
    (void)system("mkdir -p output/voice");
    
    auto unified_receiver = std::make_shared<UnifiedReceiver>(port);
    
    VoiceReceive::VoiceReceiver receiver(unified_receiver);
    
    if (!receiver.CreateOutput(output_path)) {
        std::cerr << "Failed to create output file: " << output_path << std::endl;
        return 1;
    }
    
    receiver.Start();
    unified_receiver->start();
    
    std::cout << "Voice receiver running on port " << port << std::endl;
    std::cout << "Output file: " << output_path << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    unified_receiver->stop();
    
    auto stats = receiver.GetStats();
    std::cout << "\n接收统计:" << std::endl;
    std::cout << "  帧数: " << stats.frames_received << std::endl;
    std::cout << "  字节: " << stats.bytes_received << std::endl;
    std::cout << "  输出: " << output_path << std::endl;
    
    return 0;
}

int RunVoiceSender(const std::string& addr, int port, const std::string& input_file) {
    std::string input_path = "data/voice/" + input_file;
    
    // 创建语音发送器（直接使用地址和端口，不走UnifiedSender）
    VoiceTransmit::VoiceTransmitter transmitter(addr, port);
    
    if (!transmitter.OpenFile(input_path)) {
        std::cerr << "Failed to open audio file: " << input_path << std::endl;
        return 1;
    }
    
    transmitter.Start();
    
    std::cout << "开始发送语音: " << input_path << std::endl;
    std::cout << "目标: " << addr << ":" << port << std::endl;
    std::cout << "(按 Ctrl+C 停止)" << std::endl;
    
    while (g_running && transmitter.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    transmitter.Stop();
    
    auto stats = transmitter.GetStats();
    std::cout << "\n发送统计:" << std::endl;
    std::cout << "  帧数: " << stats.frames_sent << std::endl;
    std::cout << "  字节: " << stats.bytes_sent << std::endl;
    std::cout << "语音发送完成" << std::endl;
    
    return 0;
}

// ============================================================================
// 飞控指令传输
// ============================================================================
int RunFCReceiver(int port, const std::string& log_file) {
    auto unified_receiver = std::make_shared<UnifiedReceiver>(port);
    
    FCControlReceiver receiver(unified_receiver);
    
    if (!log_file.empty()) {
        std::string log_path = GetOutputDir(DataTransmit::DataType::FC_CONTROL) + log_file;
        receiver.SetLogFile(log_path);
    }
    
    receiver.Start();
    unified_receiver->start();
    
    // 飞控接收端保持运行直到Ctrl+C
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    unified_receiver->stop();
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
    auto unified_receiver = std::make_shared<UnifiedReceiver>(port);
    
    PointCloudReceiver receiver(unified_receiver);
    
    if (!output_file.empty()) {
        std::string output_path = GetOutputDir(DataTransmit::DataType::POINT_CLOUD) + output_file;
        receiver.SetOutputFile(output_path);
    }
    
    receiver.Start();
    unified_receiver->start();
    
    std::cout << "等待接收点云数据... (按 Ctrl+C 停止)" << std::endl;
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    unified_receiver->stop();
    
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
    auto unified_receiver = std::make_shared<UnifiedReceiver>(port);
    
    GridMapReceiver receiver(unified_receiver);
    
    if (!output_file.empty()) {
        std::string output_path = GetOutputDir(DataTransmit::DataType::GRID_MAP) + output_file;
        receiver.SetOutputFile(output_path);
    }
    
    receiver.Start();
    unified_receiver->start();
    
    std::cout << "等待接收栅格地图数据... (按 Ctrl+C 停止)" << std::endl;
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    unified_receiver->stop();
    
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
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string role = argv[1];
    
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    
    // 统一接收模式（同时接收所有数据类型）
    if (role == "receiver_all") {
        int base_port = (argc > 2) ? std::stoi(argv[2]) : 9000;
        return RunUnifiedReceiver(base_port);
    }
    
    if (argc < 4) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string type = argv[2];
    
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
        } else if (type == "voice") {
            if (argc < 5) {
                std::cerr << "语音接收需要输出文件名" << std::endl;
                return 1;
            }
            return RunVoiceReceiver(port, argv[4]);
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
        } else if (type == "voice") {
            if (argc < 6) {
                std::cerr << "语音发送需要输入文件名" << std::endl;
                return 1;
            }
            return RunVoiceSender(addr, port, argv[5]);
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
