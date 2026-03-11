/**
 * video_streaming_demo.cpp - 视频传输演示程序
 * 
 * 演示视频通过网络传输的功能：
 * - 发送端：读取MP4文件，RaptorQ编码，通过网络发送
 * - 接收端：接收网络数据，RaptorQ解码，写入MP4文件
 * 
 * 使用方法:
 *   # 终端1 - 启动接收端
 *   ./video_streaming_demo receiver 9001 output.mp4
 * 
 *   # 终端2 - 启动发送端
 *   ./video_streaming_demo sender 127.0.0.1 9001 input.mp4
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>

#include "video_transmitter.h"
#include "video_receiver.h"

using namespace VideoTransmit;

static std::atomic<bool> g_running(true);

void SignalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

// 全局接收器指针用于信号处理
static VideoReceiver* g_receiver = nullptr;

void SetGlobalReceiver(VideoReceiver* receiver) {
    g_receiver = receiver;
}

void SignalHandlerWithReceiver(int signal) {
    std::cout << "\nReceived signal " << signal << ", closing video and exiting..." << std::endl;
    // 先关闭视频文件（确保MP4 trailer写入）
    if (g_receiver) {
        g_receiver->CloseOutputFile();
    }
    _exit(0);  // 立即退出，不执行其他清理
}

void PrintUsage(const char* program) {
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << program << " receiver <port> <output.mp4>   - 启动接收端" << std::endl;
    std::cout << "  " << program << " sender <host> <port> <input.mp4> - 启动发送端" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  # 终端1 - 接收端" << std::endl;
    std::cout << "  " << program << " receiver 9001 output.mp4" << std::endl;
    std::cout << std::endl;
    std::cout << "  # 终端2 - 发送端" << std::endl;
    std::cout << "  " << program << " sender 127.0.0.1 9001 input.mp4" << std::endl;
}

int RunReceiver(int argc, char* argv[]) {
    if (argc < 4) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    std::string output_file = argv[3];
    
    std::cout << "========================================" << std::endl;
    std::cout << "    Video Streaming Receiver" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Output: " << output_file << std::endl;
    std::cout << std::endl;
    
    VideoReceiver receiver(port, 4);
    SetGlobalReceiver(&receiver);  // 设置全局接收器用于信号处理
    
    // 设置回调
    receiver.SetConfigCallback([](const VideoConfig& config) {
        std::cout << "[Receiver] Video config received:" << std::endl;
        std::cout << "  Resolution: " << config.width << "x" << config.height << std::endl;
        std::cout << "  FPS: " << config.fps_num << "/" << config.fps_den << std::endl;
    });
    
    receiver.SetFrameCallback([](const VideoCodec::EncodedFrame& frame) {
        static int count = 0;
        count++;
        if (count % 30 == 0) {
            std::cout << "[Receiver] Frame " << count 
                      << " (" << VideoCodec::FrameTypeToString(frame.type)
                      << ", " << frame.data.size() << " bytes)" << std::endl;
        }
    });
    
    receiver.SetErrorCallback([](const std::string& error) {
        std::cerr << "[Receiver Error] " << error << std::endl;
    });
    
    // 启动接收
    receiver.Start();
    
    // 等待视频配置并创建输出文件（可中断）
    std::cout << "Waiting for video config..." << std::endl;
    bool config_received = false;
    int retry = 0;
    while (g_running && retry < 300) {
        VideoConfig config;
        if (receiver.GetVideoConfig(config)) {
            config_received = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retry++;
        if (retry % 10 == 0) {
            std::cout << "  waiting... (" << retry * 100 << "ms)" << std::endl;
        }
    }
    
    if (!g_running) {
        std::cout << "Interrupted while waiting for config" << std::endl;
        receiver.Stop();
        return 0;
    }
    
    if (!config_received) {
        std::cerr << "Timeout waiting for video config" << std::endl;
        receiver.Stop();
        return 1;
    }
    
    if (!receiver.CreateOutputFile(output_file)) {
        std::cerr << "Failed to create output file" << std::endl;
        receiver.Stop();
        return 1;
    }
    
    // 运行直到信号
    std::cout << "Receiving... Press Ctrl+C to stop" << std::endl;
    while (g_running && receiver.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 停止并清理
    receiver.Stop();
    receiver.CloseOutputFile();
    
    // 打印统计
    auto stats = receiver.GetStatistics();
    std::cout << std::endl;
    std::cout << "Receiver Statistics:" << std::endl;
    std::cout << "  Frames received: " << stats.frames_received << std::endl;
    std::cout << "  I-frames: " << stats.i_frames_received << std::endl;
    std::cout << "  P-frames: " << stats.p_frames_received << std::endl;
    std::cout << "  B-frames: " << stats.b_frames_received << std::endl;
    std::cout << "  Total bytes: " << stats.bytes_received << std::endl;
    
    std::cout << std::endl;
    std::cout << "Output saved to: " << output_file << std::endl;
    
    return 0;
}

int RunSender(int argc, char* argv[]) {
    if (argc < 5) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string host = argv[2];
    uint16_t port = static_cast<uint16_t>(std::stoi(argv[3]));
    std::string input_file = argv[4];
    
    std::cout << "========================================" << std::endl;
    std::cout << "    Video Streaming Sender" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Target: " << host << ":" << port << std::endl;
    std::cout << "Input: " << input_file << std::endl;
    std::cout << std::endl;
    
    VideoTransmitter transmitter(host, port, 4);
    
    // 设置回调
    transmitter.SetSendCallback([](uint32_t seq, FrameType type, size_t bytes, bool success) {
        static int count = 0;
        count++;
        if (count % 30 == 0) {
            std::cout << "[Sender] Frame " << seq 
                      << " (" << VideoCodec::FrameTypeToString(static_cast<VideoCodec::FrameType>(type))
                      << ", " << bytes << " bytes, " 
                      << (success ? "OK" : "FAIL") << ")" << std::endl;
        }
    });
    
    transmitter.SetErrorCallback([](const std::string& error) {
        std::cerr << "[Sender Error] " << error << std::endl;
    });
    
    // 打开视频文件
    if (!transmitter.OpenVideoFile(input_file)) {
        std::cerr << "Failed to open video file: " << input_file << std::endl;
        return 1;
    }
    
    // 打印视频信息
    auto info = transmitter.GetVideoInfo();
    std::cout << "Video Info:" << std::endl;
    std::cout << "  Resolution: " << info.width << "x" << info.height << std::endl;
    std::cout << "  FPS: " << info.fps_num << "/" << info.fps_den << std::endl;
    std::cout << "  GOP: " << info.gop_size << std::endl;
    std::cout << std::endl;
    
    // 启动传输
    transmitter.Start();
    
    // 等待传输完成或信号
    std::cout << "Sending... Press Ctrl+C to stop" << std::endl;
    while (g_running && transmitter.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 停止并清理
    transmitter.Stop();
    
    // 打印统计
    auto stats = transmitter.GetStatistics();
    std::cout << std::endl;
    std::cout << "Sender Statistics:" << std::endl;
    std::cout << "  Frames sent: " << stats.frames_sent << std::endl;
    std::cout << "  I-frames: " << stats.i_frames_sent << std::endl;
    std::cout << "  P-frames: " << stats.p_frames_sent << std::endl;
    std::cout << "  B-frames: " << stats.b_frames_sent << std::endl;
    std::cout << "  Symbols sent: " << stats.symbols_sent << std::endl;
    std::cout << "  Total bytes: " << stats.bytes_sent << std::endl;
    
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string mode = argv[1];
    
    if (mode == "receiver") {
        // 接收端使用带接收器的信号处理
        std::signal(SIGINT, SignalHandlerWithReceiver);
        std::signal(SIGTERM, SignalHandlerWithReceiver);
        return RunReceiver(argc, argv);
    } else if (mode == "sender") {
        // 发送端使用简单信号处理
        std::signal(SIGINT, SignalHandler);
        std::signal(SIGTERM, SignalHandler);
        return RunSender(argc, argv);
    } else {
        PrintUsage(argv[0]);
        return 1;
    }
}
