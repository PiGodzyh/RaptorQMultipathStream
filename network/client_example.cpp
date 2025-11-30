/*
 * UDP 客户端示例
 */

#include "network_client.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <memory>

using namespace Network;

int main(int argc, char* argv[]) {
    std::string server_addr = "127.0.0.1";
    uint16_t server_port = 8888;
    
    if (argc > 1) {
        server_addr = argv[1];
    }
    if (argc > 2) {
        server_port = std::atoi(argv[2]);
    }
    
    std::cout << "=== UDP 客户端示例 ===" << std::endl;
    std::cout << "目标服务器: " << server_addr << ":" << server_port << std::endl << std::endl;
    
    // 创建客户端
    UDPClient client;
    
    std::cout << "本地端口: " << client.getLocalPort() << std::endl << std::endl;
    
    // 设置默认目标
    client.setDefaultTarget(server_addr, server_port);
    
    // 设置接收回调（用于接收服务器的响应）
    client.setReceiveCallback([](std::shared_ptr<Packet> packet) {
        std::cout << "收到响应:" << std::endl;
        std::cout << "  来源: " << packet->remote_addr << ":" << packet->remote_port << std::endl;
        std::cout << "  大小: " << packet->data.size() << " 字节" << std::endl;
        
        std::string text(packet->data.begin(), packet->data.end());
        std::cout << "  内容: " << text << std::endl << std::endl;
    });
    
    // 启动接收（非阻塞）
    client.startReceiving();
    
    // 发送几条测试消息
    for (int i = 1; i <= 5; ++i) {
        std::string message = "测试消息 #" + std::to_string(i);
        std::vector<uint8_t> data(message.begin(), message.end());
        
        std::cout << "发送: " << message << std::endl;
        ssize_t sent = client.send(data);
        
        if (sent > 0) {
            std::cout << "  已发送 " << sent << " 字节" << std::endl;
        } else {
            std::cout << "  发送失败" << std::endl;
        }
        
        // 等待一会儿
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    std::cout << std::endl << "继续发送数据，按 Ctrl+C 退出..." << std::endl;
    
    // 交互模式：从标准输入读取并发送
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        
        if (line == "quit" || line == "exit") {
            break;
        }
        
        std::vector<uint8_t> data(line.begin(), line.end());
        ssize_t sent = client.send(data);
        
        if (sent > 0) {
            std::cout << "已发送 " << sent << " 字节" << std::endl;
        } else {
            std::cout << "发送失败" << std::endl;
        }
    }
    
    std::cout << "客户端退出" << std::endl;
    
    return 0;
}

