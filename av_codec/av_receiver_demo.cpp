/**
 * @file av_receiver_demo.cpp
 * @brief 音视频接收端 Demo（基础版本，无FEC）
 * 
 * 使用 network 模块接收，通过 av_codec 模块写入文件
 */

#include "av_receiver.h"
#include <iostream>
#include <signal.h>
#include <atomic>

using namespace AVCodecModule;

AVReceiver* g_receiver = nullptr;
std::atomic<bool> g_should_exit(false);
std::atomic<bool> g_finalized(false);

void signalHandler(int sig) {
    if (g_should_exit) {
        // 已经在处理退出，强制退出
        std::cout << "\n强制退出" << std::endl;
        exit(1);
    }
    
    std::cout << "\n收到信号 " << sig << "，正在停止..." << std::endl;
    g_should_exit = true;
    
    // 只设置标志，让主循环处理退出
    // 不在信号处理函数中调用复杂函数
}

void printUsage(const char* program) {
    std::cout << "用法: " << program << " <port> <output_file> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数:" << std::endl;
    std::cout << "  port           监听端口 (如: 9000)" << std::endl;
    std::cout << "  output_file    输出文件路径 (如: output.mp4)" << std::endl;
    std::cout << std::endl;
    std::cout << "可选参数:" << std::endl;
    std::cout << "  -b <size>      最大缓冲包数 (默认: 100)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " 9000 output.mp4" << std::endl;
    std::cout << "  " << program << " 9000 output.mp4 -b 200" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }
    
    // 解析参数
    uint16_t port = std::atoi(argv[1]);
    std::string output_file = argv[2];
    size_t buffer_size = 100;
    
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-b" && i + 1 < argc) {
            buffer_size = std::atoi(argv[++i]);
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  AV Receiver Demo (基础版本，无FEC)" << std::endl;
    std::cout << "  network -> av_codec" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "监听端口: " << port << std::endl;
    std::cout << "输出文件: " << output_file << std::endl;
    std::cout << "缓冲大小: " << buffer_size << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    // 创建接收器
    AVReceiver receiver(port);
    g_receiver = &receiver;
    
    // 配置
    receiver.setOutputFile(output_file);
    receiver.setMaxBufferSize(buffer_size);
    
    // 设置信号处理（确保 Ctrl+C 时文件正确关闭）
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 启动接收（阻塞，直到收到结束标记或 Ctrl+C）
    // start() 会自动处理结束标记并调用 finalize()
    receiver.start();
    
    // 如果是用户中断（Ctrl+C），需要手动完成
    if (g_should_exit && !g_finalized) {
        std::cout << "\n用户中断，正在完成文件写入..." << std::endl;
        receiver.stop();
        receiver.finalize();
        g_finalized = true;
    }
    
    // 统计
    std::cout << "\n========================================" << std::endl;
    std::cout << "接收完成:" << std::endl;
    std::cout << "  接收包数: " << receiver.getReceivedPackets() << std::endl;
    std::cout << "  接收字节: " << (receiver.getReceivedBytes() / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "  写入包数: " << receiver.getWrittenPackets() << std::endl;
    std::cout << "  输出文件: " << output_file << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
