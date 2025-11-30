/*
 * UDP 服务器实现
 */

#include "network_server.h"
#include <event2/event.h>
#include <event2/util.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <iostream>

namespace Network {

class UDPServer::Impl {
public:
    Impl(uint16_t port, const std::string& bind_addr)
        : port_(port)
        , bind_addr_(bind_addr)
        , sockfd_(-1)
        , event_base_(nullptr)
        , event_(nullptr)
        , running_(false)
    {
    }
    
    ~Impl() {
        stop();
    }
    
    bool initialize() {
        // 创建 UDP socket
        sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd_ < 0) {
            handleError("无法创建 socket");
            return false;
        }
        
        // 设置为非阻塞
        evutil_make_socket_nonblocking(sockfd_);
        
        // 设置 SO_REUSEADDR
        int reuse = 1;
        setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        
        // 绑定地址
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        
        if (bind_addr_ == "0.0.0.0" || bind_addr_.empty()) {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, bind_addr_.c_str(), &addr.sin_addr) <= 0) {
                handleError("无效的绑定地址");
                close(sockfd_);
                sockfd_ = -1;
                return false;
            }
        }
        
        if (bind(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            handleError("无法绑定到端口 " + std::to_string(port_));
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }
        
        // 如果端口是 0，获取实际分配的端口
        if (port_ == 0) {
            socklen_t addr_len = sizeof(addr);
            getsockname(sockfd_, (struct sockaddr*)&addr, &addr_len);
            port_ = ntohs(addr.sin_port);
        }
        
        return true;
    }
    
    bool start() {
        if (!initialize()) {
            return false;
        }
        
        // 创建 event_base
        event_base_ = event_base_new();
        if (!event_base_) {
            handleError("无法创建 event_base");
            return false;
        }
        
        // 创建读事件
        event_ = event_new(event_base_, sockfd_, EV_READ | EV_PERSIST,
                          readCallback, this);
        if (!event_) {
            handleError("无法创建事件");
            return false;
        }
        
        event_add(event_, nullptr);
        
        running_ = true;
        
        // 运行事件循环
        event_base_dispatch(event_base_);
        
        return true;
    }
    
    bool startAsync() {
        if (!initialize()) {
            return false;
        }
        
        // 在新线程中启动
        event_thread_ = std::thread([this]() {
            event_base_ = event_base_new();
            if (!event_base_) {
                handleError("无法创建 event_base");
                return;
            }
            
            event_ = event_new(event_base_, sockfd_, EV_READ | EV_PERSIST,
                              readCallback, this);
            if (!event_) {
                handleError("无法创建事件");
                return;
            }
            
            event_add(event_, nullptr);
            running_ = true;
            event_base_dispatch(event_base_);
        });
        
        // 等待一小段时间确保线程启动
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        return true;
    }
    
    void stop() {
        if (!running_) {
            return;
        }
        
        running_ = false;
        
        if (event_base_) {
            event_base_loopbreak(event_base_);
        }
        
        if (event_thread_.joinable()) {
            event_thread_.join();
        }
        
        if (event_) {
            event_free(event_);
            event_ = nullptr;
        }
        
        if (event_base_) {
            event_base_free(event_base_);
            event_base_ = nullptr;
        }
        
        if (sockfd_ >= 0) {
            close(sockfd_);
            sockfd_ = -1;
        }
    }
    
    ssize_t sendTo(const std::vector<uint8_t>& data, 
                   const std::string& addr, uint16_t port) {
        if (sockfd_ < 0) {
            return -1;
        }
        
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, addr.c_str(), &dest_addr.sin_addr) <= 0) {
            handleError("无效的目标地址: " + addr);
            return -1;
        }
        
        ssize_t sent = sendto(sockfd_, data.data(), data.size(), 0,
                             (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        
        if (sent < 0) {
            handleError("发送失败");
        }
        
        return sent;
    }
    
    static void readCallback(evutil_socket_t fd, short events, void* arg) {
        Impl* self = static_cast<Impl*>(arg);
        
        uint8_t buffer[DEFAULT_BUFFER_SIZE];
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        ssize_t received = recvfrom(fd, buffer, sizeof(buffer), 0,
                                   (struct sockaddr*)&client_addr, &addr_len);
        
        if (received < 0) {
            self->handleError("接收数据失败");
            return;
        }
        
        if (received == 0) {
            return;
        }
        
        // 获取客户端地址信息
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
        uint16_t client_port = ntohs(client_addr.sin_port);
        
        // 创建数据包并调用回调
        if (self->receive_callback_) {
            auto packet = std::make_shared<Packet>(buffer, received, addr_str, client_port);
            self->receive_callback_(packet);
        }
    }
    
    void handleError(const std::string& error) {
        if (error_callback_) {
            error_callback_(error);
        } else {
            std::cerr << "错误: " << error << std::endl;
        }
    }
    
    uint16_t port_;
    std::string bind_addr_;
    int sockfd_;
    struct event_base* event_base_;
    struct event* event_;
    std::atomic<bool> running_;
    std::thread event_thread_;
    
    ReceiveCallback receive_callback_;
    ErrorCallback error_callback_;
};

// ============================================================================
// UDPServer 公共接口实现
// ============================================================================

UDPServer::UDPServer(uint16_t port, const std::string& bind_addr)
    : pImpl(std::make_unique<Impl>(port, bind_addr))
{
}

UDPServer::~UDPServer() = default;

void UDPServer::setReceiveCallback(ReceiveCallback callback) {
    pImpl->receive_callback_ = callback;
}

void UDPServer::setErrorCallback(ErrorCallback callback) {
    pImpl->error_callback_ = callback;
}

bool UDPServer::start() {
    return pImpl->start();
}

bool UDPServer::startAsync() {
    return pImpl->startAsync();
}

void UDPServer::stop() {
    pImpl->stop();
}

ssize_t UDPServer::sendTo(const std::vector<uint8_t>& data,
                          const std::string& addr, uint16_t port) {
    return pImpl->sendTo(data, addr, port);
}

ssize_t UDPServer::reply(const Packet& packet) {
    return pImpl->sendTo(packet.data, packet.remote_addr, packet.remote_port);
}

bool UDPServer::isRunning() const {
    return pImpl->running_;
}

uint16_t UDPServer::getPort() const {
    return pImpl->port_;
}

std::string UDPServer::getBindAddr() const {
    return pImpl->bind_addr_;
}

} // namespace Network

