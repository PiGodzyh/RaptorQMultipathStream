/**
 * @file av_sender_demo.cpp
 * @brief 音视频发送端 Demo（基础版本，无FEC）
 * 
 * 使用 av_codec 模块读取音视频，通过 network 模块发送
 */

#include "av_sender.h"
#include <iostream>
#include <signal.h>

using namespace AVCodecModule;

AVSender* g_sender = nullptr;

void signalHandler(int sig) {
    std::cout << "\n收到信号 " << sig << "，正在停止..." << std::endl;
    if (g_sender) {
        g_sender->stop();
    }
    exit(0);
}

void printUsage(const char* program) {
    std::cout << "用法: " << program << " <media_file> <server_addr> <server_port> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数:" << std::endl;
    std::cout << "  media_file     媒体文件路径 (支持 mp4, mkv, avi, mp3, wav 等)" << std::endl;
    std::cout << "  server_addr    目标服务器地址 (如: 127.0.0.1)" << std::endl;
    std::cout << "  server_port    目标服务器端口 (如: 9000)" << std::endl;
    std::cout << std::endl;
    std::cout << "可选参数:" << std::endl;
    std::cout << "  -d <ms>        发送间隔毫秒 (默认: 0, 尽快发送)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " video.mp4 127.0.0.1 9000" << std::endl;
    std::cout << "  " << program << " video.mp4 127.0.0.1 9000 -d 10" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }
    
    // 解析参数
    std::string media_file = argv[1];
    std::string server_addr = argv[2];
    uint16_t server_port = std::atoi(argv[3]);
    uint32_t send_interval = 0;
    
    for (int i = 4; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d" && i + 1 < argc) {
            send_interval = std::atoi(argv[++i]);
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "   AV Sender Demo (基础版本，无FEC)" << std::endl;
    std::cout << "   av_codec -> network" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "媒体文件: " << media_file << std::endl;
    std::cout << "目标地址: " << server_addr << ":" << server_port << std::endl;
    std::cout << "发送间隔: " << send_interval << " ms" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    // 创建发送器
    AVSender sender(server_addr, server_port);
    g_sender = &sender;
    
    // 设置发送间隔
    sender.setSendInterval(send_interval);
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 打开媒体文件
    if (!sender.open(media_file)) {
        std::cerr << "无法打开媒体文件: " << media_file << std::endl;
        return 1;
    }
    
    // 显示媒体信息
    const MediaInfo& info = sender.getMediaInfo();
    std::cout << "媒体信息:" << std::endl;
    if (info.has_video) {
        std::cout << "  视频: " << info.video_width << "x" << info.video_height << std::endl;
    }
    if (info.has_audio) {
        std::cout << "  音频: " << info.audio_sample_rate << "Hz, " 
                  << info.audio_channels << " 声道" << std::endl;
    }
    std::cout << std::endl;
    
    // 发送
    std::cout << "开始发送..." << std::endl;
    sender.sendAll();
    
    // 统计
    std::cout << "\n========================================" << std::endl;
    std::cout << "发送完成:" << std::endl;
    std::cout << "  发送包数: " << sender.getSentPackets() << std::endl;
    std::cout << "  发送字节: " << (sender.getSentBytes() / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
