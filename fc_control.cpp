/**
 * fc_control.cpp - 飞控指令传输实现
 */

#include "fc_control.h"
#include "pack/rq_pack.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <iomanip>

namespace DataTransmit {

// ============================================================================
// FCControlTransmitter 实现
// ============================================================================

FCControlTransmitter::FCControlTransmitter(const std::string& server_addr, int server_port)
    : sender_(std::make_unique<Sender>(server_addr, server_port, 512, 1))
    , server_addr_(server_addr)
    , server_port_(server_port) {
}

FCControlTransmitter::~FCControlTransmitter() {
    Stop();
}

void FCControlTransmitter::Run() {
    running_ = true;
    sender_->start();
    
    std::cout << "========================================" << std::endl;
    std::cout << "   飞控指令发送端" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "输入指令发送（输入 'quit' 退出）:" << std::endl;
    std::cout << "示例: TAKEOFF, LAND, MOVE 1.0 2.0 3.0" << std::endl;
    std::cout << "优先级: !high <cmd>, !normal <cmd>, !low <cmd>" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    std::string line;
    while (running_ && std::getline(std::cin, line)) {
        if (line == "quit" || line == "exit") {
            break;
        }
        if (line.empty()) {
            continue;
        }
        
        // 解析优先级前缀 (!high, !normal, !low)
        uint8_t priority = FCControlHeader::PRIORITY_NORMAL;
        std::string cmd = line;
        if (line.substr(0, 6) == "!high ") {
            priority = FCControlHeader::PRIORITY_HIGH;
            cmd = line.substr(6);
        } else if (line.substr(0, 8) == "!normal ") {
            priority = FCControlHeader::PRIORITY_NORMAL;
            cmd = line.substr(8);
        } else if (line.substr(0, 5) == "!low ") {
            priority = FCControlHeader::PRIORITY_LOW;
            cmd = line.substr(5);
        }
        
        FCControlPacket packet;
        packet.command = cmd;
        
        if (SendCommand(cmd, priority)) {
            std::cout << "[发送] " << cmd << " (优先级:" << (int)priority << ")" << std::endl;
        } else {
            std::cout << "[失败] 发送失败: " << cmd << std::endl;
        }
    }
    
    std::cout << "飞控发送端已停止" << std::endl;
}

void FCControlTransmitter::Stop() {
    running_ = false;
    sender_->stop();
}

bool FCControlTransmitter::SendCommand(const std::string& command, uint8_t priority) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    
    FCControlPacket packet;
    packet.command = command;
    
    uint32_t seq = seq_counter_++;
    return SendFrame(packet, seq);
}

bool FCControlTransmitter::SendFrame(const FCControlPacket& packet, uint32_t seq) {
    // 构建数据包
    FCControlHeader header;
    header.timestamp = GetCurrentTimestampUs();
    header.seq = seq;
    header.cmd_len = packet.command.length();
    header.priority = FCControlHeader::PRIORITY_NORMAL;
    memset(header.reserved, 0, sizeof(header.reserved));
    
    // 序列化
    std::vector<uint8_t> data(sizeof(FCControlHeader) + packet.command.length());
    memcpy(data.data(), &header, sizeof(FCControlHeader));
    memcpy(data.data() + sizeof(FCControlHeader), packet.command.c_str(), packet.command.length());
    
    // FEC编码 - 50%冗余
    FECParams fec = FECParams::FCParams();
    RQPack::Encoder encoder(data.data(), data.size(), fec.symbol_size);
    
    uint32_t source_count = encoder.getSourceSymbolCount();
    uint32_t repair_count = static_cast<uint32_t>(source_count * fec.redundancy_ratio);
    auto symbols = encoder.encodeAll(repair_count);
    
    // 发送符号
    uint32_t stream_id = seq + 1;  // stream_id从1开始，0保留
    return sender_->sendSymbols(stream_id, symbols, data.size(), fec.symbol_size);
}

// ============================================================================
// FCControlReceiver 实现
// ============================================================================

FCControlReceiver::FCControlReceiver(int listen_port)
    : receiver_(std::make_unique<Receiver>(this, listen_port, 1)) {
}

FCControlReceiver::~FCControlReceiver() {
    Stop();
}

void FCControlReceiver::Start() {
    running_ = true;
    
    // 在单独线程中启动接收器（因为start()是阻塞的）
    std::thread receiver_thread([this]() {
        receiver_->start();
    });
    receiver_thread.detach();
    
    std::cout << "========================================" << std::endl;
    std::cout << "   飞控指令接收端" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "等待接收指令... (按 Ctrl+C 退出)" << std::endl;
}

void FCControlReceiver::Stop() {
    running_ = false;
    receiver_->stop();
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void FCControlReceiver::SetLogFile(const std::string& log_path) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_.is_open()) {
        log_file_.close();
    }
    log_file_.open(log_path, std::ios::out | std::ios::app);
}

void FCControlReceiver::OnDecodeComplete(uint32_t stream_id, 
                                        const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(FCControlHeader)) {
        return;
    }
    
    // 解析头部
    FCControlHeader header;
    memcpy(&header, data.data(), sizeof(FCControlHeader));
    
    // 解析指令
    std::string command(
        reinterpret_cast<const char*>(data.data() + sizeof(FCControlHeader)),
        header.cmd_len
    );
    
    // 计算延迟
    uint64_t now = GetCurrentTimestampUs();
    int64_t delay_us = now - header.timestamp;
    double delay_ms = delay_us / 1000.0;
    
    received_count_++;
    
    // 输出到终端
    std::cout << "[FC][" << header.seq << "][优先级" << (int)header.priority 
              << "][延迟" << std::fixed << std::setprecision(2) << delay_ms << "ms] " 
              << command << std::endl;
    
    // 写入日志
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_.is_open()) {
        log_file_ << header.timestamp << "," << header.seq << ","
                  << (int)header.priority << "," << delay_ms << ","
                  << command << std::endl;
    }
}

} // namespace DataTransmit
