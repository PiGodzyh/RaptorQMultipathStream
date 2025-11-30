/*
 * UDP 服务器示例
 */

#include "network_server.h"
#include <iostream>
#include <iomanip>
#include <signal.h>
#include <memory>

using namespace Network;

UDPServer* server_ptr = nullptr;

void signalHandler(int sig) {
    std::cout << "\n接收到信号 " << sig << "，正在停止服务器..." << std::endl;
    if (server_ptr) {
        server_ptr->stop();
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 8888;
    
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    
    std::cout << "=== UDP 服务器示例 ===" << std::endl;
    std::cout << "监听端口: " << port << std::endl << std::endl;
    
    // 创建服务器
    UDPServer server(port);
    server_ptr = &server;
    
    // 设置 Ctrl+C 信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 设置接收回调
    server.setReceiveCallback([&server](std::shared_ptr<Packet> packet) {
        std::cout << "收到数据:" << std::endl;
        std::cout << "  来源: " << packet->remote_addr << ":" << packet->remote_port << std::endl;
        std::cout << "  大小: " << packet->data.size() << " 字节" << std::endl;
        std::cout << "  内容: ";
        
        // 打印前 64 字节的十六进制
        size_t print_len = std::min(packet->data.size(), size_t(64));
        for (size_t i = 0; i < print_len; ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << static_cast<int>(packet->data[i]) << " ";
            if ((i + 1) % 16 == 0) std::cout << std::endl << "         ";
        }
        std::cout << std::dec << std::endl;
        
        // 尝试将数据作为字符串打印
        std::string text(packet->data.begin(), packet->data.end());
        std::cout << "  文本: " << text << std::endl << std::endl;
        
        // 回复确认消息
        std::string reply = "ACK: 收到 " + std::to_string(packet->data.size()) + " 字节";
        std::vector<uint8_t> reply_data(reply.begin(), reply.end());
        Packet reply_packet(reply_data, packet->remote_addr, packet->remote_port);
        server.reply(reply_packet);
        
        std::cout << "已回复确认消息" << std::endl << std::endl;
    });
    
    // 设置错误回调
    server.setErrorCallback([](const std::string& error) {
        std::cerr << "错误: " << error << std::endl;
    });
    
    std::cout << "服务器正在运行，按 Ctrl+C 停止..." << std::endl << std::endl;
    
    // 启动服务器（阻塞）
    if (!server.start()) {
        std::cerr << "服务器启动失败" << std::endl;
        return 1;
    }
    
    std::cout << "服务器已停止" << std::endl;
    
    return 0;
}

