/*
 * UDP 客户端 - 基于 libevent
 */

#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include "network_common.h"
#include <memory>

namespace Network {

/**
 * UDP 客户端类
 * 用于发送 UDP 数据包
 */
class UDPClient {
public:
    /**
     * 构造函数
     * 
     * @param bind_port: 本地绑定端口，0 表示自动分配
     */
    explicit UDPClient(uint16_t bind_port = 0);
    
    ~UDPClient();
    
    // 禁止拷贝
    UDPClient(const UDPClient&) = delete;
    UDPClient& operator=(const UDPClient&) = delete;
    
    /**
     * 发送数据到指定地址
     * 
     * @param data: 要发送的数据
     * @param addr: 目标地址
     * @param port: 目标端口
     * @return: 发送的字节数，失败返回 -1
     */
    ssize_t sendTo(const std::vector<uint8_t>& data, 
                   const std::string& addr, uint16_t port);
    
    /**
     * 发送数据到指定地址（原始指针版本）
     * 
     * @param data: 要发送的数据指针
     * @param len: 数据长度
     * @param addr: 目标地址
     * @param port: 目标端口
     * @return: 发送的字节数，失败返回 -1
     */
    ssize_t sendTo(const uint8_t* data, size_t len,
                   const std::string& addr, uint16_t port);
    
    /**
     * 设置默认目标地址
     * 设置后可以使用 send() 方法直接发送到默认地址
     * 
     * @param addr: 默认目标地址
     * @param port: 默认目标端口
     */
    void setDefaultTarget(const std::string& addr, uint16_t port);
    
    /**
     * 发送数据到默认目标地址
     * 需要先调用 setDefaultTarget() 设置默认地址
     * 
     * @param data: 要发送的数据
     * @return: 发送的字节数，失败返回 -1
     */
    ssize_t send(const std::vector<uint8_t>& data);
    
    /**
     * 发送数据到默认目标地址（原始指针版本）
     * 
     * @param data: 要发送的数据指针
     * @param len: 数据长度
     * @return: 发送的字节数，失败返回 -1
     */
    ssize_t send(const uint8_t* data, size_t len);
    
    /**
     * 设置接收回调（可选）
     * 如果需要接收响应数据，可以设置此回调
     */
    void setReceiveCallback(ReceiveCallback callback);
    
    /**
     * 设置错误回调
     */
    void setErrorCallback(ErrorCallback callback);
    
    /**
     * 启动事件循环以接收数据（可选，非阻塞）
     * 只有在需要接收响应时才需要调用
     */
    bool startReceiving();
    
    /**
     * 停止接收
     */
    void stopReceiving();
    
    /**
     * 获取本地绑定端口
     */
    uint16_t getLocalPort() const;
    
    /**
     * 检查是否正在接收
     */
    bool isReceiving() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace Network

#endif // NETWORK_CLIENT_H

