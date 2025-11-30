/*
 * 网络通信公共定义
 */

#ifndef NETWORK_COMMON_H
#define NETWORK_COMMON_H

#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include <memory>

namespace Network {

/**
 * 数据包结构
 */
struct Packet {
    std::vector<uint8_t> data;      // 数据内容
    std::string remote_addr;        // 远程地址
    uint16_t remote_port;           // 远程端口
    
    Packet() : remote_port(0) {}
    
    Packet(const uint8_t* buf, size_t len, 
           const std::string& addr = "", uint16_t port = 0)
        : data(buf, buf + len), remote_addr(addr), remote_port(port) {}
    
    Packet(const std::vector<uint8_t>& d, 
           const std::string& addr = "", uint16_t port = 0)
        : data(d), remote_addr(addr), remote_port(port) {}
};

/**
 * 接收回调函数类型
 * 参数：接收到的数据包
 */
using ReceiveCallback = std::function<void(std::shared_ptr<Packet> packet)>;

/**
 * 错误回调函数类型
 * 参数：错误消息
 */
using ErrorCallback = std::function<void(const std::string& error)>;

// 默认配置
constexpr size_t DEFAULT_BUFFER_SIZE = 65536;  // 64KB
constexpr int DEFAULT_TIMEOUT_SEC = 30;

} // namespace Network

#endif // NETWORK_COMMON_H

