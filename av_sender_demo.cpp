/**
 * @file av_sender_demo.cpp
 * @brief 音视频发送端 Demo（带FEC版本）
 * 
 * 流程：
 * 1. 使用 av_codec 模块读取音视频文件
 * 2. 使用 SendCenter (RaptorQ FEC + network) 进行编码和传输
 * 
 * 对比 av_codec/av_sender_demo.cpp 只使用基础网络传输，
 * 本 demo 增加了 RaptorQ FEC 编码，提高传输可靠性
 */

#include "send_center.h"
#include "av_codec/av_codec.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <atomic>

using namespace AVCodecModule;

SendCenter* g_send_center = nullptr;
std::atomic<bool> g_should_stop(false);

void signalHandler(int sig) {
    std::cout << "\n收到信号 " << sig << "，正在停止..." << std::endl;
    g_should_stop = true;
    // 不在信号处理函数中直接调用 stop()，让主循环处理
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
    std::cout << "  -s <size>      RaptorQ符号大小字节 (默认: 1024)" << std::endl;
    std::cout << "  -r <ratio>     FEC修复符号比例 0.0-1.0 (默认: 0.2)" << std::endl;
    std::cout << "  -t <threads>   RaptorQ编码线程数 (默认: 4)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " video.mp4 127.0.0.1 9000" << std::endl;
    std::cout << "  " << program << " video.mp4 127.0.0.1 9000 -s 2048 -r 0.3" << std::endl;
    std::cout << std::endl;
    std::cout << "注意: 本程序使用 RaptorQ FEC 编码提高可靠性" << std::endl;
    std::cout << "      基础版本(无FEC)请使用 av_codec/build/av_sender_demo" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }
    
    // 解析必需参数
    std::string media_file = argv[1];
    std::string server_addr = argv[2];
    uint16_t server_port = std::atoi(argv[3]);
    
    // 默认参数
    uint16_t symbol_size = 1024;
    float repair_ratio = 0.2f;
    uint32_t encode_threads = 4;
    
    // 解析可选参数
    for (int i = 4; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-s" && i + 1 < argc) {
            symbol_size = std::atoi(argv[++i]);
        } else if (arg == "-r" && i + 1 < argc) {
            repair_ratio = std::atof(argv[++i]);
        } else if (arg == "-t" && i + 1 < argc) {
            encode_threads = std::atoi(argv[++i]);
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "   AV Sender Demo (带 RaptorQ FEC)" << std::endl;
    std::cout << "   av_codec -> RaptorQ -> Network" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "媒体文件: " << media_file << std::endl;
    std::cout << "目标地址: " << server_addr << ":" << server_port << std::endl;
    std::cout << "RaptorQ符号大小: " << symbol_size << " 字节" << std::endl;
    std::cout << "FEC修复比例: " << (repair_ratio * 100) << "%" << std::endl;
    std::cout << "编码线程: " << encode_threads << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    // ========== 1. 使用 av_codec 打开媒体文件 ==========
    std::cout << "[1] 打开媒体文件..." << std::endl;
    MediaReader reader;
    if (!reader.open(media_file)) {
        std::cerr << "无法打开媒体文件: " << media_file << std::endl;
        return 1;
    }
    
    const MediaInfo& info = reader.getMediaInfo();
    std::cout << "媒体信息:" << std::endl;
    if (info.has_video) {
        std::cout << "  视频: " << info.video_width << "x" << info.video_height << std::endl;
        std::cout << "  编码: " << avcodec_get_name(info.video_codec_id) << std::endl;
    }
    if (info.has_audio) {
        std::cout << "  音频: " << info.audio_sample_rate << "Hz, " 
                  << info.audio_channels << " 声道" << std::endl;
    }
    std::cout << "  时长: " << (info.duration / 1000000.0) << " 秒" << std::endl << std::endl;
    
    // ========== 2. 创建 SendCenter (RaptorQ + Network) ==========
    std::cout << "[2] 初始化 RaptorQ 编码器和网络..." << std::endl;
    SendCenter send_center(server_addr, server_port, symbol_size, encode_threads);
    g_send_center = &send_center;
    send_center.setRepairRatio(repair_ratio);
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 启动发送器
    send_center.start();
    std::cout << "RaptorQ 发送器已启动" << std::endl << std::endl;
    
    // ========== 3. 发送媒体头 ==========
    std::cout << "[3] 发送媒体头..." << std::endl;
    MediaHeader header = reader.getMediaHeader();
    std::vector<uint8_t> header_data = header.serialize();
    
    // 用特殊标记封装头部（index = 0xFFFFFFFF 表示头部）
    MediaPacket header_pkt;
    header_pkt.type = FrameType::UNKNOWN;
    header_pkt.index = 0xFFFFFFFF;
    header_pkt.data = header_data;
    
    std::vector<uint8_t> header_serialized = header_pkt.serialize();
    auto header_str = std::make_shared<std::string>(
        reinterpret_cast<const char*>(header_serialized.data()),
        header_serialized.size()
    );
    
    if (!send_center.sendData(header_str)) {
        std::cerr << "发送媒体头失败" << std::endl;
        return 1;
    }
    std::cout << "✓ 媒体头已发送 (" << header_str->size() << " 字节)" << std::endl << std::endl;
    
    // 等待头部发送
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // ========== 4. 读取并发送所有媒体包 ==========
    std::cout << "[4] 开始发送媒体数据 (RaptorQ 编码)..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();
    
    uint64_t video_packets = 0;
    uint64_t audio_packets = 0;
    uint64_t total_bytes = 0;
    
    MediaPacket packet;
    while (!g_should_stop && reader.readPacket(packet)) {
        // 序列化媒体包
        std::vector<uint8_t> pkt_data = packet.serialize();
        auto pkt_str = std::make_shared<std::string>(
            reinterpret_cast<const char*>(pkt_data.data()),
            pkt_data.size()
        );
        
        // 通过 RaptorQ + Network 发送
        if (send_center.sendData(pkt_str)) {
            total_bytes += pkt_str->size();
            
            if (packet.type == FrameType::VIDEO) {
                video_packets++;
                if (video_packets % 30 == 0) {
                    std::cout << "\r进度: 视频=" << video_packets 
                              << ", 音频=" << audio_packets
                              << ", 队列=" << send_center.getQueueSize()
                              << "        " << std::flush;
                }
            } else if (packet.type == FrameType::AUDIO) {
                audio_packets++;
            }
        }
        
        // 控制发送速度，避免队列过大
        while (!g_should_stop && send_center.getQueueSize() > 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    std::cout << "\n\n[5] 等待发送队列清空..." << std::endl;
    while (!g_should_stop && send_center.getQueueSize() > 0) {
        std::cout << "\r队列剩余: " << send_center.getQueueSize() << "        " << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // ========== 5. 发送结束标记 ==========
    if (!g_should_stop) {
        std::cout << "\n\n[6] 发送结束标记..." << std::endl;
        MediaPacket eos_packet;
        eos_packet.type = FrameType::END_OF_STREAM;
        eos_packet.index = static_cast<uint32_t>(video_packets + audio_packets);
        eos_packet.pts = 0;
        eos_packet.dts = 0;
        eos_packet.is_key_frame = false;
        
        std::vector<uint8_t> eos_data = eos_packet.serialize();
        auto eos_str = std::make_shared<std::string>(
            reinterpret_cast<const char*>(eos_data.data()),
            eos_data.size()
        );
        
        // 发送多次确保接收端收到
        for (int i = 0; i < 3 && !g_should_stop; i++) {
            if (send_center.sendData(eos_str)) {
                std::cout << "已发送结束标记 (" << (i + 1) << "/3)" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // 等待最后的数据发送完成
    std::cout << "等待发送完成..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 停止
    send_center.stop();
    reader.close();
    
    // ========== 统计 ==========
    std::cout << "\n========================================" << std::endl;
    std::cout << "发送完成统计:" << std::endl;
    std::cout << "  视频包数: " << video_packets << std::endl;
    std::cout << "  音频包数: " << audio_packets << std::endl;
    std::cout << "  原始数据: " << (total_bytes / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "  RaptorQ符号: " << send_center.getSentSymbolCount() << std::endl;
    std::cout << "  发送失败: " << send_center.getFailedCount() << std::endl;
    std::cout << "  总耗时: " << (duration.count() / 1000.0) << " 秒" << std::endl;
    if (duration.count() > 0) {
        double mbps = (total_bytes * 8.0 / 1000000.0) / (duration.count() / 1000.0);
        std::cout << "  平均速率: " << mbps << " Mbps" << std::endl;
    }
    std::cout << "========================================" << std::endl;
    
    return 0;
}
