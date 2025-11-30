/*
 * Sender Demo
 * 使用 RaptorQ FEC 编码发送数据
 */

#include "send_center.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <sstream>

SendCenter* send_center_ptr = nullptr;

void signalHandler(int sig) {
    std::cout << "\n收到信号 " << sig << "，正在停止..." << std::endl;
    if (send_center_ptr) {
        send_center_ptr->stop();
    }
    exit(0);
}

void printUsage(const char* program) {
    std::cout << "用法: " << program << " <server_addr> <server_port> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数:" << std::endl;
    std::cout << "  server_addr    目标服务器地址 (如: 127.0.0.1)" << std::endl;
    std::cout << "  server_port    目标服务器端口 (如: 9000)" << std::endl;
    std::cout << std::endl;
    std::cout << "可选参数:" << std::endl;
    std::cout << "  -n <count>     发送数据包数量 (默认: 10)" << std::endl;
    std::cout << "  -i <ms>        发送间隔毫秒 (默认: 1000)" << std::endl;
    std::cout << "  -s <size>      符号大小字节 (默认: 256)" << std::endl;
    std::cout << "  -r <ratio>     修复符号比例 0.0-1.0 (默认: 0.1)" << std::endl;
    std::cout << "  -t <threads>   编码线程数 (默认: 4)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " 127.0.0.1 9000" << std::endl;
    std::cout << "  " << program << " 127.0.0.1 9000 -n 20 -i 500 -s 512 -r 0.2 -t 8" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }
    
    // 解析必需参数
    std::string server_addr = argv[1];
    uint16_t server_port = std::atoi(argv[2]);
    
    // 默认参数
    int count = 10;
    int interval_ms = 100;
    uint16_t symbol_size = 256;
    float repair_ratio = 0.1f;
    uint32_t encode_threads = 4;
    
    // 解析可选参数
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-n" && i + 1 < argc) {
            count = std::atoi(argv[++i]);
        } else if (arg == "-i" && i + 1 < argc) {
            interval_ms = std::atoi(argv[++i]);
        } else if (arg == "-s" && i + 1 < argc) {
            symbol_size = std::atoi(argv[++i]);
        } else if (arg == "-r" && i + 1 < argc) {
            repair_ratio = std::atof(argv[++i]);
        } else if (arg == "-t" && i + 1 < argc) {
            encode_threads = std::atoi(argv[++i]);
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "   Sender Demo (RaptorQ FEC)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "目标地址: " << server_addr << ":" << server_port << std::endl;
    std::cout << "发送数量: " << count << std::endl;
    std::cout << "发送间隔: " << interval_ms << " ms" << std::endl;
    std::cout << "符号大小: " << symbol_size << " 字节" << std::endl;
    std::cout << "修复比例: " << (repair_ratio * 100) << "%" << std::endl;
    std::cout << "编码线程: " << encode_threads << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    // 创建发送中心
    SendCenter send_center(server_addr, server_port, symbol_size, encode_threads);
    send_center_ptr = &send_center;
    
    // 设置修复符号比例
    send_center.setRepairRatio(repair_ratio);
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 启动发送器
    send_center.start();
    
    std::cout << "开始发送数据..." << std::endl << std::endl;
    
    // 发送数据
    auto start_time = std::chrono::steady_clock::now();
    
    for (int i = 0; i < count; ++i) {
        // 构造测试数据
        std::ostringstream oss;
        oss << "Message #" << i << " [" 
            << std::chrono::system_clock::now().time_since_epoch().count() << "]: ";
        
        // 添加内容使数据更大（模拟真实数据）
        for (int j = 0; j < 50; ++j) {
            oss << "This is test data segment " << j << ". ";
        }
        oss << "END";
        
        auto data = std::make_shared<std::string>(oss.str());
        
        // 发送（stream_id由SendCenter自动分配）
        if (send_center.sendData(data)) {
            std::cout << "✓ 已放入队列 #" << i 
                      << " (大小: " << data->size() << " 字节"
                      << ", 队列: " << send_center.getQueueSize() << ")" << std::endl;
        } else {
            std::cerr << "✗ 放入队列失败 #" << i << std::endl;
        }
        
        // 延时
        if (interval_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    }
    
    std::cout << "\n所有数据已放入队列，等待编码和发送完成..." << std::endl;
    
    // 等待队列清空
    while (send_center.getQueueSize() > 0) {
        std::cout << "队列剩余: " << send_center.getQueueSize() << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 停止发送器
    send_center.stop();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "发送完成统计:" << std::endl;
    std::cout << "  数据包数: " << send_center.getSentCount() << std::endl;
    std::cout << "  符号总数: " << send_center.getSentSymbolCount() << std::endl;
    std::cout << "  发送失败: " << send_center.getFailedCount() << std::endl;
    std::cout << "  总耗时: " << duration.count() << " ms" << std::endl;
    if (send_center.getSentSymbolCount() > 0) {
        std::cout << "  平均速率: " 
                  << (send_center.getSentSymbolCount() * 1000.0 / duration.count()) 
                  << " 符号/秒" << std::endl;
    }
    std::cout << "========================================" << std::endl;
    
    return 0;
}

