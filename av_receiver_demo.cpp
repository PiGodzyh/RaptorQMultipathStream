/**
 * @file av_receiver_demo.cpp
 * @brief 音视频接收端 Demo（带FEC版本）
 * 
 * 流程：
 * 1. 使用 ReceiverCenter (network + RaptorQ) 接收和 FEC 解码
 * 2. 使用 av_codec 模块写入音视频文件
 * 
 * 本 demo 使用缓冲模式：
 * - 接收时将包缓冲到内存
 * - 收到结束标记后排序并写入文件
 * - 确保视频文件结构正确
 */

#include "receiver_center.h"
#include "av_codec/av_codec.h"
#include <iostream>
#include <signal.h>
#include <mutex>
#include <atomic>
#include <queue>
#include <thread>
#include <chrono>

using namespace AVCodecModule;

ReceiverCenter* g_receiver_center = nullptr;
std::atomic<bool> g_should_stop(false);
std::atomic<bool> g_eos_received(false);
std::atomic<bool> g_finalized(false);

void signalHandler(int sig) {
    if (g_should_stop) {
        // 已经在处理退出，强制退出
        std::cout << "\n强制退出" << std::endl;
        exit(1);
    }
    std::cout << "\n收到信号 " << sig << "，正在停止..." << std::endl;
    g_should_stop = true;
    // 不在信号处理函数中直接调用复杂函数
}

void printUsage(const char* program) {
    std::cout << "用法: " << program << " <port> <output_file> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数:" << std::endl;
    std::cout << "  port           监听端口 (如: 9000)" << std::endl;
    std::cout << "  output_file    输出文件路径 (如: output.mp4)" << std::endl;
    std::cout << std::endl;
    std::cout << "可选参数:" << std::endl;
    std::cout << "  -t <count>     RaptorQ解码线程数量 (默认: 4)" << std::endl;
    std::cout << "  -v             详细模式" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " 9000 output.mp4" << std::endl;
    std::cout << "  " << program << " 9000 output.mp4 -t 8 -v" << std::endl;
    std::cout << std::endl;
    std::cout << "注意: 本程序使用 RaptorQ FEC 解码提高可靠性" << std::endl;
    std::cout << "      基础版本(无FEC)请使用 av_codec/build/av_receiver_demo" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }
    
    // 解析必需参数
    uint16_t port = std::atoi(argv[1]);
    std::string output_file = argv[2];
    
    // 默认参数
    uint32_t thread_count = 4;
    bool verbose = false;
    
    // 解析可选参数
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-t" && i + 1 < argc) {
            thread_count = std::atoi(argv[++i]);
        } else if (arg == "-v") {
            verbose = true;
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "   AV Receiver Demo (带 RaptorQ FEC)" << std::endl;
    std::cout << "   Network -> RaptorQ -> av_codec" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "监听端口: " << port << std::endl;
    std::cout << "输出文件: " << output_file << std::endl;
    std::cout << "解码线程: " << thread_count << std::endl;
    std::cout << "详细模式: " << (verbose ? "开启" : "关闭") << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    // 状态变量
    std::atomic<bool> header_received(false);
    std::atomic<uint64_t> video_packets(0);
    std::atomic<uint64_t> audio_packets(0);
    std::atomic<uint64_t> total_bytes(0);
    
    // av_codec 写入器（使用缓冲模式）
    MediaWriter writer;
    std::mutex writer_mutex;
    
    // ========== 1. 创建 ReceiverCenter (Network + RaptorQ) ==========
    std::cout << "[1] 初始化网络接收器和 RaptorQ 解码器..." << std::endl;
    ReceiverCenter receiver_center(port, thread_count);
    g_receiver_center = &receiver_center;
    
    // 设置数据回调
    receiver_center.setMsgCallback(
        [&](const std::vector<uint8_t>& data) {
            total_bytes += data.size();
            
            // 反序列化 MediaPacket
            MediaPacket packet;
            if (!MediaPacket::deserialize(data, packet)) {
                if (verbose) {
                    std::cerr << "无法解析数据包" << std::endl;
                }
                return;
            }
            
            // 检查是否是结束标记
            if (packet.type == FrameType::END_OF_STREAM) {
                std::cout << "\n✓ 收到结束标记" << std::endl;
                g_eos_received = true;
                return;
            }
            
            // 检查是否是媒体头（特殊标记 index = 0xFFFFFFFF）
            if (packet.index == 0xFFFFFFFF) {
                MediaHeader header;
                if (!MediaHeader::deserialize(packet.data, header)) {
                    std::cerr << "无法解析媒体头" << std::endl;
                    return;
                }
                
                std::lock_guard<std::mutex> lock(writer_mutex);
                
                if (!header_received) {
                    // 初始化 av_codec 写入器（缓冲模式）
                    if (!writer.initialize(header)) {
                        std::cerr << "初始化写入器失败" << std::endl;
                        return;
                    }
                    
                    if (!writer.open(output_file)) {
                        std::cerr << "打开输出文件失败: " << output_file << std::endl;
                        return;
                    }
                    
                    header_received = true;
                    
                    std::cout << "\n✓ 收到媒体头，av_codec 写入器已初始化" << std::endl;
                    if (header.info.has_video) {
                        std::cout << "  视频: " << header.info.video_width 
                                  << "x" << header.info.video_height << std::endl;
                        std::cout << "  编码: " << avcodec_get_name(header.info.video_codec_id) << std::endl;
                    }
                    if (header.info.has_audio) {
                        std::cout << "  音频: " << header.info.audio_sample_rate 
                                  << "Hz, " << header.info.audio_channels 
                                  << " 声道" << std::endl;
                    }
                    std::cout << std::endl;
                }
                return;
            }
            
            // 普通媒体包 - 直接写入（缓冲模式会自动缓存）
            if (!header_received) {
                return;  // 还没收到头，先忽略
            }
            
            std::lock_guard<std::mutex> lock(writer_mutex);
            
            if (writer.writePacket(packet)) {
                if (packet.type == FrameType::VIDEO) {
                    video_packets++;
                    if (video_packets % 30 == 0) {
                        std::cout << "\r进度: 视频=" << video_packets 
                                  << ", 音频=" << audio_packets
                                  << ", 缓冲=" << writer.getPacketCount()
                                  << "        " << std::flush;
                    }
                } else if (packet.type == FrameType::AUDIO) {
                    audio_packets++;
                }
                
                if (verbose && packet.type == FrameType::VIDEO && packet.is_key_frame) {
                    std::cout << "\n关键帧 #" << packet.index 
                              << " (PTS=" << packet.pts << ")" << std::endl;
                }
            }
        }
    );
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // ========== 2. 启动接收 ==========
    std::cout << "[2] 启动接收器..." << std::endl;
    std::cout << "等待接收音视频数据..." << std::endl;
    std::cout << "按 Ctrl+C 停止" << std::endl << std::endl;
    
    // 在后台线程中启动接收
    std::thread receiver_thread([&]() {
        receiver_center.start();
    });
    
    // 等待结束标记或用户中断
    while (!g_should_stop && !g_eos_received) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 等待最后的数据处理完成（在停止前等待）
    std::cout << "\n等待数据处理完成..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // ========== 3. 先完成文件写入 ==========
    std::cout << "\n[3] 完成输出..." << std::endl;
    
    if (header_received && !g_finalized) {
        std::lock_guard<std::mutex> lock(writer_mutex);
        
        std::cout << "缓冲包数: " << writer.getPacketCount() << std::endl;
        std::cout << "正在排序并写入文件..." << std::endl;
        
        // finalize 会将缓冲区中的包排序后写入
        if (writer.finalize()) {
            std::cout << "✓ 输出文件已保存: " << output_file << std::endl;
        } else {
            std::cerr << "写入文件失败" << std::endl;
        }
        
        writer.close();
        g_finalized = true;
    }
    
    // 停止接收（文件已保存后再停止）
    std::cout << "\n正在停止接收..." << std::endl;
    receiver_center.stop();
    
    // 等待接收线程结束（设置超时）
    if (receiver_thread.joinable()) {
        // 分离线程，不阻塞等待（避免卡住）
        receiver_thread.detach();
    }
    
    // 短暂等待清理
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // ========== 统计 ==========
    std::cout << "\n========================================" << std::endl;
    std::cout << "接收完成统计:" << std::endl;
    std::cout << "  接收数据包: " << receiver_center.getReceivedCount() << std::endl;
    std::cout << "  RaptorQ解码流: " << receiver_center.getDecodedStreamCount() << std::endl;
    std::cout << "  视频包数: " << video_packets << std::endl;
    std::cout << "  音频包数: " << audio_packets << std::endl;
    std::cout << "  关键帧数: " << writer.getKeyFrameCount() << std::endl;
    std::cout << "  数据总量: " << (total_bytes / 1024.0 / 1024.0) << " MB" << std::endl;
    if (header_received) {
        std::cout << "  输出文件: " << output_file << std::endl;
    }
    std::cout << "========================================" << std::endl;
    
    return 0;
}
