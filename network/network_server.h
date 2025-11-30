/*
 * UDP 服务器 - 基于 libevent
 */

#ifndef NETWORK_SERVER_H
#define NETWORK_SERVER_H

#include "network_common.h"
#include <memory>

namespace Network {

/**
 * UDP 服务器类
 * 用于监听指定端口并接收 UDP 数据包
 */
class UDPServer {
public:
    /**
     * 构造函数
     * 
     * @param port: 监听端口
     * @param bind_addr: 绑定地址，默认 "0.0.0.0" 表示监听所有接口
     */
    explicit UDPServer(uint16_t port, const std::string& bind_addr = "0.0.0.0");
    
    ~UDPServer();
    
    // 禁止拷贝
    UDPServer(const UDPServer&) = delete;
    UDPServer& operator=(const UDPServer&) = delete;
    
    /**
     * 设置接收回调函数
     * 当接收到数据包时会调用此回调
     */
    void setReceiveCallback(ReceiveCallback callback);
    
    /**
     * 设置错误回调函数
     * 当发生错误时会调用此回调
     */
    void setErrorCallback(ErrorCallback callback);
    
    /**
     * 启动服务器（阻塞）
     * 此函数会阻塞直到调用 stop()
     * 
     * @return: 成功返回 true，失败返回 false
     */
    bool start();
    
    /**
     * 启动服务器（非阻塞）
     * 在新线程中运行事件循环
     * 
     * @return: 成功返回 true，失败返回 false
     */
    bool startAsync();
    
    /**
     * 停止服务器
     */
    void stop();
    
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
     * 回复数据包（发送到数据包的来源地址）
     * 
     * @param packet: 要回复的数据包（会使用其中的地址信息）
     * @return: 发送的字节数，失败返回 -1
     */
    ssize_t reply(const Packet& packet);
    
    /**
     * 检查服务器是否正在运行
     */
    bool isRunning() const;
    
    /**
     * 获取监听端口
     */
    uint16_t getPort() const;
    
    /**
     * 获取绑定地址
     */
    std::string getBindAddr() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace Network

#endif // NETWORK_SERVER_H

