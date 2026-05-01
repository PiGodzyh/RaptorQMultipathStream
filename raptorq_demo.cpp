/**
 * multi_stream_test.cpp - 多流并发测试
 * 
 * 同时启动多个数据流发送，验证 UnifiedSender 的优先级调度和带宽分配
 * 
 * 用法:
 *   ./multi_stream_test receiver          # 启动接收端（统一接收5种数据）
 *   ./multi_stream_test sender <types>    # 启动发送端（如：fc,voice,video）
 * 
 * 示例:
 *   ./multi_stream_test sender fc,voice      # 同时发送FC+语音
 *   ./multi_stream_test sender all           # 同时发送5种数据类型
 */

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <sstream>
#include <iomanip>

#include "log.h"
#include "unified_sender.h"
#include "unified_receiver.h"
#include "data_common.h"

#include "video_transmitter.h"
#include "video_receiver.h"
#include "voice_transmitter.h"
#include "voice_receiver.h"
#include "fc_control.h"
#include "point_cloud.h"
#include "grid_map.h"

using namespace DataTransmit;
using namespace VideoTransmit;
using namespace VoiceTransmit;
using namespace VoiceReceive;

static std::atomic<bool> g_running(true);

void SignalHandler(int signal) {
    g_running = false;
}

// 解析数据类型列表
std::vector<DataPriority> ParseTypes(const std::string& types_str) {
    std::vector<DataPriority> types;
    std::stringstream ss(types_str);
    std::string type;
    
    while (std::getline(ss, type, ',')) {
        if (type == "fc" || type == "FC") types.push_back(DataPriority::FC_COMMAND);
        else if (type == "voice") types.push_back(DataPriority::VOICE);
        else if (type == "video") types.push_back(DataPriority::VIDEO);
        else if (type == "pointcloud" || type == "pc") types.push_back(DataPriority::POINT_CLOUD);
        else if (type == "gridmap" || type == "gm") types.push_back(DataPriority::GRID_MAP);
        else if (type == "all") {
            types = {DataPriority::FC_COMMAND, DataPriority::VOICE, DataPriority::VIDEO,
                     DataPriority::POINT_CLOUD, DataPriority::GRID_MAP};
            break;
        }
    }
    return types;
}



// ==================== 模拟数据发送（论文测试用）====================

void SendFCDataSim(UnifiedSender* sender, int duration_sec) {
    uint32_t seq = 0;
    auto start = std::chrono::steady_clock::now();
    while (g_running) {
        std::string cmd = "CMD_" + std::to_string(seq);
        std::vector<uint8_t> data(cmd.begin(), cmd.end());
        sender->send(DataPriority::FC_COMMAND, seq++, data);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() >= duration_sec) break;
    }
}

void SendVoiceDataSim(UnifiedSender* sender, int duration_sec) {
    uint32_t seq = 0;
    auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> frame(320);
    while (g_running) {
        for (size_t i = 0; i < frame.size(); ++i) frame[i] = (seq * 320 + i) % 256;
        sender->send(DataPriority::VOICE, seq++, frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() >= duration_sec) break;
    }
}

void SendVideoDataSim(UnifiedSender* sender, int duration_sec) {
    uint32_t seq = 0;
    auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> frame(10000);
    while (g_running) {
        for (size_t i = 0; i < frame.size(); ++i) frame[i] = (seq * 10000 + i) % 256;
        sender->send(DataPriority::VIDEO, seq++, frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
        if (std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() >= duration_sec) break;
    }
}

void SendPointCloudDataSim(UnifiedSender* sender, int duration_sec) {
    uint32_t seq = 0;
    auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> frame(5000);
    while (g_running) {
        for (size_t i = 0; i < frame.size(); ++i) frame[i] = (seq * 5000 + i) % 256;
        sender->send(DataPriority::POINT_CLOUD, seq++, frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() >= duration_sec) break;
    }
}

void SendGridMapDataSim(UnifiedSender* sender, int duration_sec) {
    uint32_t seq = 0;
    auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> frame(10000);
    while (g_running) {
        for (size_t i = 0; i < frame.size(); ++i) frame[i] = (seq * 10000 + i) % 256;
        sender->send(DataPriority::GRID_MAP, seq++, frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count() >= duration_sec) break;
    }
}

// ==================== 真实数据发送 ====================

void RunVideoSenderReal(std::shared_ptr<UnifiedSender> sender) {
    VideoTransmit::VideoTransmitter transmitter(sender);
    transmitter.SetLogFile("logs/video_tx.log");
    if (!transmitter.OpenVideoFile("data/videos/test_gop1s.mp4")) {
        std::cerr << "[Video] Failed to open video file" << std::endl;
        return;
    }
    transmitter.Start();
    
    // 保持存活直到程序终止（防止局部变量析构导致发送线程被 Stop）
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    transmitter.Stop();
}

void RunVoiceSenderReal(std::shared_ptr<UnifiedSender> sender) {
    VoiceTransmit::VoiceTransmitter transmitter(sender);
    if (!transmitter.OpenFile("data/voice/test_8k.wav")) {
        std::cerr << "[Voice] Failed to open audio file" << std::endl;
        return;
    }
    transmitter.Start();
    
    // 保持存活直到程序终止
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    transmitter.Stop();
}

void RunPointCloudSenderReal(std::shared_ptr<UnifiedSender> sender) {
    PointCloudTransmitter transmitter(sender);
    transmitter.SendTestCloud(1000, 10);
}

void RunGridMapSenderReal(std::shared_ptr<UnifiedSender> sender) {
    GridMapTransmitter transmitter(sender);
    transmitter.SendFromFile("data/gridmap/office_200x200.grid");
}

void RunFCSenderReal(std::shared_ptr<UnifiedSender> sender) {
    FCControlTransmitter transmitter(sender);
    transmitter.SetLogFile("logs/fc_tx.log");
    transmitter.Run();  // 交互式输入
}

int RunSender(const std::vector<DataPriority>& types, 
              const std::string& target_ip,
              bool use_simulation,
              bool bypass_fec,
              float redundancy,
              float drop_rate) {
    if (types.empty()) {
        std::cerr << "No valid types specified" << std::endl;
        return 1;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  RaptorQ Multi-Stream Sender" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Target: " << target_ip << std::endl;
    std::cout << "Types: ";
    for (auto& t : types) std::cout << PriorityToName(t) << " ";
    std::cout << std::endl;
    std::cout << "Mode: " << (use_simulation ? "Simulation" : "Real Data") << std::endl;
    std::cout << "Bypass FEC: " << (bypass_fec ? "Yes" : "No") << std::endl;
    std::cout << "Redundancy: " << (redundancy * 100) << "%" << std::endl;
    if (drop_rate > 0.0f) {
        std::cout << "Drop Rate: " << (drop_rate * 100) << "% (simulated)" << std::endl;
    }
    std::cout << "========================================" << std::endl;
    
    UnifiedSenderConfig config;
    config.target_ip = target_ip;
    auto sender = std::make_shared<UnifiedSender>(config);
    
    if (!sender->initialize()) {
        std::cerr << "Failed to initialize sender" << std::endl;
        return 1;
    }
    
    sender->SetLogFile("logs/sender.log");
    
    // 设置 Bypass 模式和冗余度
    if (bypass_fec) {
        sender->setBypassFec(true);
    }
    if (redundancy >= 0.0f) {
        for (int i = 0; i < 5; ++i) {
            sender->setRedundancy(static_cast<DataPriority>(i), redundancy);
        }
    }
    if (drop_rate > 0.0f) {
        sender->setDropRate(drop_rate);
    }
    
    sender->start();
    
    std::vector<std::thread> threads;
    
    if (use_simulation) {
        // 模拟数据模式
        for (auto& type : types) {
            switch (type) {
                case DataPriority::FC_COMMAND:
                    threads.emplace_back(SendFCDataSim, sender.get(), 10); break;
                case DataPriority::VOICE:
                    threads.emplace_back(SendVoiceDataSim, sender.get(), 10); break;
                case DataPriority::VIDEO:
                    threads.emplace_back(SendVideoDataSim, sender.get(), 10); break;
                case DataPriority::POINT_CLOUD:
                    threads.emplace_back(SendPointCloudDataSim, sender.get(), 10); break;
                case DataPriority::GRID_MAP:
                    threads.emplace_back(SendGridMapDataSim, sender.get(), 10); break;
                default: break;
            }
        }
    } else {
        // 真实数据模式
        for (auto& type : types) {
            switch (type) {
                case DataPriority::FC_COMMAND:
                    threads.emplace_back(RunFCSenderReal, sender); break;
                case DataPriority::VOICE:
                    threads.emplace_back(RunVoiceSenderReal, sender); break;
                case DataPriority::VIDEO:
                    threads.emplace_back(RunVideoSenderReal, sender); break;
                case DataPriority::POINT_CLOUD:
                    threads.emplace_back(RunPointCloudSenderReal, sender); break;
                case DataPriority::GRID_MAP:
                    threads.emplace_back(RunGridMapSenderReal, sender); break;
                default: break;
            }
        }
    }
    
    std::cout << "Sending... Press Ctrl+C to stop" << std::endl;
    
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    
    sender->stop();
    
    std::cout << "\n发送完成" << std::endl;
    return 0;
}

// ==================== 接收端 ====================

int RunReceiver(bool bypass_fec) {
    std::cout << "========================================" << std::endl;
    std::cout << "  RaptorQ Multi-Stream Receiver" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Listening on ports 9000-9004" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 创建输出目录
    (void)system("mkdir -p output/videos output/voice output/pointclouds output/gridmaps output/fc");
    
    // 创建统一的 UnifiedReceiver
    auto unified_receiver = std::make_shared<UnifiedReceiver>();
    
    // 创建5种专用接收器
    FCControlReceiver fc_receiver(unified_receiver);
    VoiceReceiver voice_receiver(unified_receiver);
    PointCloudReceiver pointcloud_receiver(unified_receiver);
    VideoReceiver video_receiver(unified_receiver);
    video_receiver.SetLogFile("logs/video_rx.log");
    GridMapReceiver gridmap_receiver(unified_receiver);
    
    // 设置输出
    std::string video_output = "output/videos/received.mp4";
    std::string voice_output = "output/voice/received.wav";
    std::string pc_output = "output/pointclouds/received.pcd";
    std::string gm_output = "output/gridmaps/received.grid";
    std::string fc_output = "output/fc/received.log";
    
    // 启动各专用接收器（必须在UnifiedReceiver启动前）
    fc_receiver.Start();
    fc_receiver.SetLogFile(fc_output);
    
    voice_receiver.Start();
    voice_receiver.CreateOutput(voice_output);
    
    video_receiver.Start();
    
    pointcloud_receiver.Start();
    pointcloud_receiver.SetOutputFile(pc_output);
    
    gridmap_receiver.Start();
    gridmap_receiver.SetOutputFile(gm_output);
    
    // 设置 Bypass 模式
    if (bypass_fec) {
        unified_receiver->setBypassFec(true);
        video_receiver.setBypassMode(true);
    }
    
    // 启动 UnifiedReceiver（开始接收数据）
    unified_receiver->start();
    
    // 创建视频输出文件和实时显示管道
    std::atomic<bool> file_created{false};
    std::atomic<bool> pipe_created{false};
    
    std::thread file_thread([&video_receiver, &video_output, &file_created]() {
        for (int retry = 0; retry < 300 && !file_created; ++retry) {
            if (video_receiver.CreateOutputFile(video_output)) {
                file_created = true;
                std::cout << "[Video] Output file created: " << video_output << std::endl;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    
    std::thread pipe_thread([&video_receiver, &pipe_created]() {
        for (int retry = 0; retry < 300 && !pipe_created; ++retry) {
            if (video_receiver.CreateLivePipe("/tmp/video_live.h264")) {
                pipe_created = true;
                std::cout << "[Video] Live pipe created: /tmp/video_live.h264" << std::endl;
                std::cout << "  Run: ./start_live_view.sh (after sender starts)" << std::endl;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    
    // 主循环：打印统计
    auto start_time = std::chrono::steady_clock::now();
    int print_counter = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!g_running) break;
        
        // 每3秒打印一次统计
        if (++print_counter < 6) continue;
        print_counter = 0;
        
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();
        
        auto stats = unified_receiver->getStatistics();
        
        std::cout << "\n[统计] 运行时间: " << elapsed << " 秒" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        std::cout << std::setw(12) << "类型" 
                  << std::setw(10) << "帧数" 
                  << std::setw(12) << "速率" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        
        uint64_t total_frames = 0;
        for (int i = 0; i < 5; ++i) {
            auto prio = static_cast<DataPriority>(i);
            auto it = stats.per_priority_frames.find(prio);
            uint64_t frames = (it != stats.per_priority_frames.end()) ? it->second : 0;
            double rate = (elapsed > 0) ? (frames * 1.0 / elapsed) : 0;
            std::cout << std::setw(12) << PriorityToName(prio)
                      << std::setw(10) << frames
                      << std::setw(9) << std::fixed << std::setprecision(1) << rate << " fps"
                      << std::endl;
            total_frames += frames;
        }
        
        double total_rate = (elapsed > 0) ? (total_frames * 1.0 / elapsed) : 0;
        std::cout << "----------------------------------------" << std::endl;
        std::cout << std::setw(12) << "总计"
                  << std::setw(10) << total_frames
                  << std::setw(9) << total_rate << " fps"
                  << std::endl;
        std::cout << "  Packets: " << stats.total_packets_received
                  << "  Decoded: " << stats.total_frames_decoded
                  << "  Bytes: " << stats.total_bytes_received << std::endl;
    }
    
    // 清理
    std::cout << "[Cleanup] Joining file_thread..." << std::endl;
    file_thread.join();
    std::cout << "[Cleanup] Joining pipe_thread..." << std::endl;
    pipe_thread.join();
    std::cout << "[Cleanup] Stopping receivers..." << std::endl;
    
    fc_receiver.Stop();
    voice_receiver.Stop();
    video_receiver.Stop();
    
    // 在 pointcloud_receiver.Stop() 之前手动关闭视频文件
    //（workaround: PointCloudReceiver::StopViewer() 的 viewer_thread_.join() 可能卡住，
    //  导致后续代码无法执行）
    std::cout << "[Cleanup] Closing video output..." << std::endl;
    video_receiver.CloseOutputFile();
    video_receiver.CloseLivePipe();
    
    pointcloud_receiver.Stop();
    gridmap_receiver.Stop();
    
    std::cout << "[Cleanup] Stopping unified_receiver..." << std::endl;
    unified_receiver->stop();
    std::cout << "[Cleanup] Done" << std::endl;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "最终统计:" << std::endl;
    unified_receiver->printStatistics();
    
    return 0;
}

// ==================== 主函数 ====================

void PrintUsage(const char* program) {
    std::cout << "RaptorQ Multi-Stream Demo" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << program << " receiver [options]                # 启动接收端（统一接收5种）" << std::endl;
    std::cout << "  " << program << " sender <ip> <types> [options]     # 启动发送端" << std::endl;
    std::cout << std::endl;
    std::cout << "Types (comma separated):" << std::endl;
    std::cout << "  fc, voice, video, pointcloud, gridmap, all" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --simulation         使用模拟数据（默认使用真实文件数据）" << std::endl;
    std::cout << "  --verbose, -v        启用详细日志输出（DEBUG级别）" << std::endl;
    std::cout << "  --bypass-fec         绕过 RaptorQ FEC，直接发送原始数据" << std::endl;
    std::cout << "  --redundancy <ratio> 设置 FEC 冗余度（0.0-1.0，默认各类型不同）" << std::endl;
    std::cout << "  --drop-rate <rate>    模拟网络丢包率（0.0-1.0）" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " sender 127.0.0.1 all                    # 发送5种真实数据" << std::endl;
    std::cout << "  " << program << " sender 127.0.0.1 all --simulation       # 发送5种模拟数据" << std::endl;
    std::cout << "  " << program << " sender 127.0.0.1 video --bypass-fec     # 直接发送原始视频（无FEC）" << std::endl;
    std::cout << "  " << program << " sender 127.0.0.1 video --redundancy 0.5 # 50%冗余度" << std::endl;
    std::cout << "  " << program << " receiver --bypass-fec                   # 接收端配合bypass模式" << std::endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string mode = argv[1];
    
    if (mode == "receiver") {
        bool bypass_fec = false;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--bypass-fec" || arg == "--bypass") {
                bypass_fec = true;
            } else if (arg == "--verbose" || arg == "-v") {
                GetLogConfig().SetGlobalLevel(LogLevel::DEBUG);
            }
        }
        return RunReceiver(bypass_fec);
    } else if (mode == "sender" && argc >= 4) {
        std::string target_ip = argv[2];
        std::string types_str = argv[3];
        bool use_simulation = false;
        bool bypass_fec = false;
        float redundancy = -1.0f;  // -1 表示使用默认冗余度
        float drop_rate = 0.0f;
        
        // 检查可选参数
        for (int i = 4; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--simulation" || arg == "-s") {
                use_simulation = true;
            } else if (arg == "--verbose" || arg == "-v") {
                GetLogConfig().SetGlobalLevel(LogLevel::DEBUG);
            } else if (arg == "--bypass-fec" || arg == "--bypass") {
                bypass_fec = true;
            } else if (arg == "--redundancy" && i + 1 < argc) {
                redundancy = std::stof(argv[++i]);
                if (redundancy < 0.0f || redundancy > 1.0f) {
                    std::cerr << "Error: redundancy must be between 0.0 and 1.0" << std::endl;
                    return 1;
                }
            } else if (arg == "--drop-rate" && i + 1 < argc) {
                drop_rate = std::stof(argv[++i]);
                if (drop_rate < 0.0f || drop_rate > 1.0f) {
                    std::cerr << "Error: drop-rate must be between 0.0 and 1.0" << std::endl;
                    return 1;
                }
            }
        }
        
        auto types = ParseTypes(types_str);
        return RunSender(types, target_ip, use_simulation, bypass_fec, redundancy, drop_rate);
    } else {
        PrintUsage(argv[0]);
        return 1;
    }
}
