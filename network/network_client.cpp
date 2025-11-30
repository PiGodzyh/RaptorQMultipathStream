/*
 * UDP 客户端实现
 */

#include "network_client.h"
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

class UDPClient::Impl {
public:
    Impl(uint16_t bind_port)
        : bind_port_(bind_port)
        , local_port_(0)
        , sockfd_(-1)
        , event_base_(nullptr)
        , event_(nullptr)
        , receiving_(false)
        , default_port_(0)
    {
        initialize();
    }
    
    ~Impl() {
        stopReceiving();
        if (sockfd_ >= 0) {
            close(sockfd_);
        }
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
        
        // 如果指定了绑定端口，则绑定
        if (bind_port_ > 0) {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(bind_port_);
            
            if (bind(sockfd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                handleError("无法绑定到端口 " + std::to_string(bind_port_));
                close(sockfd_);
                sockfd_ = -1;
                return false;
            }
        }
        
        // 获取本地端口
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        getsockname(sockfd_, (struct sockaddr*)&addr, &addr_len);
        local_port_ = ntohs(addr.sin_port);
        
        return true;
    }
    
    ssize_t sendTo(const uint8_t* data, size_t len,
                   const std::string& addr, uint16_t port) {
        if (sockfd_ < 0) {
            handleError("Socket 未初始化");
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
        
        ssize_t sent = sendto(sockfd_, data, len, 0,
                             (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        
        if (sent < 0) {
            handleError("发送失败");
        }
        
        return sent;
    }
    
    bool startReceiving() {
        if (receiving_) {
            return true;
        }
        
        // 在新线程中启动事件循环
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
            receiving_ = true;
            event_base_dispatch(event_base_);
        });
        
        // 等待一小段时间确保线程启动
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        return true;
    }
    
    void stopReceiving() {
        if (!receiving_) {
            return;
        }
        
        receiving_ = false;
        
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
    }
    
    static void readCallback(evutil_socket_t fd, short events, void* arg) {
        Impl* self = static_cast<Impl*>(arg);
        
        uint8_t buffer[DEFAULT_BUFFER_SIZE];
        struct sockaddr_in remote_addr;
        socklen_t addr_len = sizeof(remote_addr);
        
        ssize_t received = recvfrom(fd, buffer, sizeof(buffer), 0,
                                   (struct sockaddr*)&remote_addr, &addr_len);
        
        if (received < 0) {
            self->handleError("接收数据失败");
            return;
        }
        
        if (received == 0) {
            return;
        }
        
        // 获取远程地址信息
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &remote_addr.sin_addr, addr_str, sizeof(addr_str));
        uint16_t remote_port = ntohs(remote_addr.sin_port);
        
        // 创建数据包并调用回调
        if (self->receive_callback_) {
            auto packet = std::make_shared<Packet>(buffer, received, addr_str, remote_port);
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
    
    uint16_t bind_port_;
    uint16_t local_port_;
    int sockfd_;
    struct event_base* event_base_;
    struct event* event_;
    std::atomic<bool> receiving_;
    std::thread event_thread_;
    
    std::string default_addr_;
    uint16_t default_port_;
    
    ReceiveCallback receive_callback_;
    ErrorCallback error_callback_;
};

// ============================================================================
// UDPClient 公共接口实现
// ============================================================================

UDPClient::UDPClient(uint16_t bind_port)
    : pImpl(std::make_unique<Impl>(bind_port))
{
}

UDPClient::~UDPClient() = default;

ssize_t UDPClient::sendTo(const std::vector<uint8_t>& data,
                          const std::string& addr, uint16_t port) {
    return pImpl->sendTo(data.data(), data.size(), addr, port);
}

ssize_t UDPClient::sendTo(const uint8_t* data, size_t len,
                          const std::string& addr, uint16_t port) {
    return pImpl->sendTo(data, len, addr, port);
}

void UDPClient::setDefaultTarget(const std::string& addr, uint16_t port) {
    pImpl->default_addr_ = addr;
    pImpl->default_port_ = port;
}

ssize_t UDPClient::send(const std::vector<uint8_t>& data) {
    if (pImpl->default_addr_.empty() || pImpl->default_port_ == 0) {
        pImpl->handleError("未设置默认目标地址");
        return -1;
    }
    return pImpl->sendTo(data.data(), data.size(), 
                        pImpl->default_addr_, pImpl->default_port_);
}

ssize_t UDPClient::send(const uint8_t* data, size_t len) {
    if (pImpl->default_addr_.empty() || pImpl->default_port_ == 0) {
        pImpl->handleError("未设置默认目标地址");
        return -1;
    }
    return pImpl->sendTo(data, len, pImpl->default_addr_, pImpl->default_port_);
}

void UDPClient::setReceiveCallback(ReceiveCallback callback) {
    pImpl->receive_callback_ = callback;
}

void UDPClient::setErrorCallback(ErrorCallback callback) {
    pImpl->error_callback_ = callback;
}

bool UDPClient::startReceiving() {
    return pImpl->startReceiving();
}

void UDPClient::stopReceiving() {
    pImpl->stopReceiving();
}

uint16_t UDPClient::getLocalPort() const {
    return pImpl->local_port_;
}

bool UDPClient::isReceiving() const {
    return pImpl->receiving_;
}

} // namespace Network

