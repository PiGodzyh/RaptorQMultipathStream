/*
 * Receiver Demo
 * 接收并解码 RaptorQ FEC 编码的数据
 */

#include "receiver_center.h"
#include <iostream>
#include <iomanip>
#include <signal.h>
#include <fstream>
#include <sstream>

ReceiverCenter* receiver_center_ptr = nullptr;

void signalHandler(int sig) {
    std::cout << "\n收到信号 " << sig << "，正在停止..." << std::endl;
    if (receiver_center_ptr) {
        receiver_center_ptr->stop();
    }
}

void printUsage(const char* program) {
    std::cout << "用法: " << program << " <port> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数:" << std::endl;
    std::cout << "  port           监听端口 (如: 9000)" << std::endl;
    std::cout << std::endl;
    std::cout << "可选参数:" << std::endl;
    std::cout << "  -t <count>     工作线程数量 (默认: 4)" << std::endl;
    std::cout << "  -o <dir>       输出目录 (默认: 当前目录)" << std::endl;
    std::cout << "  -v             详细模式" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " 9000" << std::endl;
    std::cout << "  " << program << " 9000 -t 8 -o ./output -v" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    // 解析必需参数
    uint16_t port = std::atoi(argv[1]);
    
    // 默认参数
    uint32_t thread_count = 4;
    std::string output_dir = ".";
    bool verbose = false;
    
    // 解析可选参数
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-t" && i + 1 < argc) {
            thread_count = std::atoi(argv[++i]);
        } else if (arg == "-o" && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "-v") {
            verbose = true;
        }
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  Receiver Demo (RaptorQ FEC)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "监听端口: " << port << std::endl;
    std::cout << "工作线程: " << thread_count << std::endl;
    std::cout << "输出目录: " << output_dir << std::endl;
    std::cout << "详细模式: " << (verbose ? "开启" : "关闭") << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    // 创建接收中心
    ReceiverCenter receiver_center(port, thread_count);
    receiver_center_ptr = &receiver_center;
    
    // 设置用户回调
    receiver_center.setMsgCallback(
        [&output_dir, verbose](const std::vector<uint8_t>& data) {
            std::cout << "\n========================================" << std::endl;
            std::cout << "✓ 数据解码完成！" << std::endl;
            std::cout << "  数据大小: " << data.size() << " 字节" << std::endl;
            
            // 尝试将数据转换为字符串
            std::string text(data.begin(), data.end());
            
            // 查找消息编号
            size_t msg_start = text.find("Message #");
            size_t msg_end = text.find(" [", msg_start);
            std::string msg_num = "unknown";
            if (msg_start != std::string::npos && msg_end != std::string::npos) {
                msg_num = text.substr(msg_start + 9, msg_end - msg_start - 9);
            }
            
            // 显示内容预览
            if (verbose || text.size() <= 200) {
                std::cout << "  内容: " << text.substr(0, 200);
                if (text.size() > 200) {
                    std::cout << "...";
                }
                std::cout << std::endl;
            } else {
                std::cout << "  内容预览: " << text.substr(0, 100) << "..." << std::endl;
            }
            
            // 保存到文件（可选）
            // std::ostringstream filename;
            // filename << output_dir << "/received_stream_" 
            //          << std::setfill('0') << std::setw(4) << stream_id 
            //          << "_msg_" << msg_num << ".dat";
            
            // std::ofstream outfile(filename.str(), std::ios::binary);
            // if (outfile) {
            //     outfile.write(reinterpret_cast<const char*>(data.data()), data.size());
            //     std::cout << "  已保存到: " << filename.str() << std::endl;
            // } else {
            //     std::cerr << "  保存失败: " << filename.str() << std::endl;
            // }
            
            std::cout << "========================================" << std::endl << std::endl;
        }
    );
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "接收器正在运行..." << std::endl;
    std::cout << "按 Ctrl+C 停止" << std::endl << std::endl;
    
    // 启动接收中心（阻塞）
    receiver_center.start();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "接收完成统计:" << std::endl;
    std::cout << "  接收数据包: " << receiver_center.getReceivedCount() << std::endl;
    std::cout << "  处理数据包: " << receiver_center.getProcessedCount() << std::endl;
    std::cout << "  解码完成流: " << receiver_center.getDecodedStreamCount() << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}

