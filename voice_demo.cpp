/**
 * voice_demo.cpp - 语音传输演示（文件模式）
 * 
 * 使用方式：
 *   接收端：./voice_demo receive <port> <output.wav>
 *   发送端：./voice_demo send <ip> <port> <input.wav>
 */

#include "voice_transmitter.h"
#include "voice_receiver.h"
#include "unified_receiver.h"
#include <iostream>
#include <cstdlib>
#include <signal.h>

using namespace DataTransmit;

static std::atomic<bool> g_running(true);

void SignalHandler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
    g_running = false;
}

int RunReceiver(int argc, char* argv[]) {
    uint16_t port = 9004;
    std::string output_path = "output/voice/received.wav";
    
    if (argc >= 3) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }
    if (argc >= 4) {
        output_path = argv[3];
    }
    
    auto unified_receiver = std::make_shared<UnifiedReceiver>(port);
    unified_receiver->start();
    
    VoiceReceive::VoiceReceiver receiver(unified_receiver);
    
    if (!receiver.CreateOutput(output_path)) {
        std::cerr << "Failed to create output file: " << output_path << std::endl;
        return 1;
    }
    
    receiver.Start();
    
    std::cout << "Voice receiver running on port " << port << std::endl;
    std::cout << "Output file: " << output_path << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // 主循环
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    receiver.Stop();
    unified_receiver->stop();
    return 0;
}

int RunTransmitter(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    uint16_t port = 9004;
    std::string input_path;
    
    if (argc >= 3) {
        ip = argv[2];
    }
    if (argc >= 4) {
        port = static_cast<uint16_t>(std::atoi(argv[3]));
    }
    if (argc >= 5) {
        input_path = argv[4];
    } else {
        // 使用测试文件
        input_path = "data/voice/test_8k.wav";
    }
    
    VoiceTransmit::VoiceTransmitter transmitter(ip, port);
    
    if (!transmitter.OpenFile(input_path)) {
        std::cerr << "Failed to open input file: " << input_path << std::endl;
        return 1;
    }
    
    transmitter.Start();
    
    std::cout << "Voice transmitter running, sending to " << ip << ":" << port << std::endl;
    std::cout << "Input file: " << input_path << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // 主循环 - 等待传输完成或用户中断
    while (g_running && transmitter.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // 检查是否传输完成
        if (transmitter.GetCurrentFrame() >= transmitter.GetTotalFrames() && 
            transmitter.GetTotalFrames() > 0) {
            std::cout << "\nTransmission complete!" << std::endl;
            break;
        }
    }
    
    transmitter.Stop();
    return 0;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    if (argc < 2) {
        std::cout << "Voice Transmission Demo (File Mode)" << std::endl;
        std::cout << std::endl;
        std::cout << "Usage:" << std::endl;
        std::cout << "  Receiver: " << argv[0] << " receive <port> <output.wav>" << std::endl;
        std::cout << "  Sender:   " << argv[0] << " send <ip> <port> <input.wav>" << std::endl;
        std::cout << std::endl;
        std::cout << "Examples:" << std::endl;
        std::cout << "  # Receive" << std::endl;
        std::cout << "  " << argv[0] << " receive 9004 output.wav" << std::endl;
        std::cout << std::endl;
        std::cout << "  # Send" << std::endl;
        std::cout << "  " << argv[0] << " send 127.0.0.1 9004 input.wav" << std::endl;
        return 1;
    }
    
    std::string mode = argv[1];
    
    // 支持 receiver/receive 和 sender/send
    if (mode == "receive" || mode == "receiver") {
        return RunReceiver(argc, argv);
    } else if (mode == "send" || mode == "sender") {
        return RunTransmitter(argc, argv);
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        std::cerr << "Usage: " << argv[0] << " receive|send [args...]" << std::endl;
        return 1;
    }
}
