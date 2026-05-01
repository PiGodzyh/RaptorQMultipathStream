/**
 * fc_control.cpp - 飞控指令传输实现
 */

#include "fc_control.h"

#include "unified_receiver.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <iomanip>

namespace DataTransmit {

// ============================================================================
// FCControlTransmitter 实现
// ============================================================================

FCControlTransmitter::FCControlTransmitter(std::shared_ptr<UnifiedSender> unified_sender)
    : unified_sender_(unified_sender), seq_counter_(0), running_(false) {
}

FCControlTransmitter::~FCControlTransmitter() {
    Stop();
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void FCControlTransmitter::Run() {
    running_ = true;
    
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
}

void FCControlTransmitter::SetLogFile(const std::string& log_path) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_.is_open()) {
        log_file_.close();
    }
    log_file_.open(log_path, std::ios::out | std::ios::app);
}

bool FCControlTransmitter::SendCommand(const std::string& command, uint8_t priority) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    
    FCControlPacket packet;
    packet.command = command;
    
    uint32_t seq = seq_counter_++;
    uint64_t ts = GetCurrentTimestampUs();
    bool ok = SendFrame(packet, seq, ts);
    
    // 写入发送日志（使用与 header 相同的时间戳）
    {
        std::lock_guard<std::mutex> log_lock(log_mutex_);
        if (log_file_.is_open()) {
            log_file_ << ts << "," << seq << "," << (int)priority << "," << command << std::endl;
        }
    }
    
    return ok;
}

bool FCControlTransmitter::SendFrame(const FCControlPacket& packet, uint32_t seq, uint64_t timestamp) {
    // 构建数据包
    FCControlHeader header;
    header.timestamp = timestamp;
    header.seq = seq;
    header.cmd_len = packet.command.length();
    header.priority = FCControlHeader::PRIORITY_NORMAL;
    memset(header.reserved, 0, sizeof(header.reserved));
    
    // 序列化
    std::vector<uint8_t> data(sizeof(FCControlHeader) + packet.command.length());
    memcpy(data.data(), &header, sizeof(FCControlHeader));
    memcpy(data.data() + sizeof(FCControlHeader), packet.command.c_str(), packet.command.length());
    
    // 使用 UnifiedSender 发送（内部会自动进行 RaptorQ 编码）
    uint32_t stream_id = seq + 1;  // stream_id从1开始，0保留
    return unified_sender_->send(DataPriority::FC_COMMAND, stream_id, data);
}

// ============================================================================
// FCControlReceiver 实现
// ============================================================================

FCControlReceiver::FCControlReceiver(std::shared_ptr<DataTransmit::UnifiedReceiver> unified_receiver)
    : unified_receiver_(unified_receiver) {
}

FCControlReceiver::~FCControlReceiver() {
    Stop();
}

void FCControlReceiver::Start() {
    running_ = true;
    
    // 设置解码回调（使用多回调注册API）
    callback_id_ = unified_receiver_->registerDecodeCallback([this](DataPriority priority, uint32_t stream_id,
                                                 const std::vector<uint8_t>& data) {
        if (priority == DataPriority::FC_COMMAND) {
            OnFrameReceived(priority, stream_id, data);
        }
    });
    
    std::cout << "========================================" << std::endl;
    std::cout << "   飞控指令接收端" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "等待接收指令... (按 Ctrl+C 退出)" << std::endl;
}

void FCControlReceiver::Stop() {
    running_ = false;
    
    // 注销回调
    if (callback_id_ >= 0) {
        unified_receiver_->unregisterDecodeCallback(callback_id_);
        callback_id_ = -1;
    }
    
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

void FCControlReceiver::OnFrameReceived(DataPriority priority, uint32_t stream_id,
                                        const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(FCControlHeader)) {
        std::cerr << "[FC] 数据包过小: " << data.size() << " < " << sizeof(FCControlHeader) << std::endl;
        return;
    }
    
    // 解析头部
    FCControlHeader header;
    memcpy(&header, data.data(), sizeof(FCControlHeader));
    
    // 检查 cmd_len 是否合法
    size_t cmd_data_size = data.size() - sizeof(FCControlHeader);
    if (header.cmd_len > cmd_data_size || header.cmd_len > 1024) {
        std::cerr << "[FC] 非法的 cmd_len: " << header.cmd_len << ", 可用空间: " << cmd_data_size << std::endl;
        return;
    }
    
    // 解析指令（确保只读取 cmd_len 字节）
    std::string command(
        reinterpret_cast<const char*>(data.data() + sizeof(FCControlHeader)),
        header.cmd_len
    );
    
    // 过滤不可打印字符，防止乱码
    for (auto& c : command) {
        if (c < 32 || c > 126) {
            c = '?';
        }
    }
    
    // 计算延迟（增加有效性检查）
    uint64_t now = GetCurrentTimestampUs();
    double delay_ms = 0.0;
    
    // DEBUG: 输出原始时间戳值
    static bool debug_once = true;
    if (debug_once) {
        std::cerr << "[DEBUG] header.timestamp=" << header.timestamp 
                  << ", now=" << now 
                  << ", data.size=" << data.size() << std::endl;
        debug_once = false;
    }
    
    if (header.timestamp > 0 && header.timestamp <= now) {
        delay_ms = (now - header.timestamp) / 1000.0;
    } else if (header.timestamp > now) {
        // 时间戳来自未来（时钟不同步），显示为负值
        delay_ms = -(static_cast<double>(header.timestamp - now) / 1000.0);
    }
    
    received_count_++;
    
    // 输出到终端
    std::cout << "[FC][" << header.seq << "][优先级" << (int)header.priority 
              << "][延迟" << std::fixed << std::setprecision(2) << delay_ms << "ms] " 
              << command << std::endl;
    
    // 写入日志（第一列为接收时刻的时间戳，方便与发送端对比计算延迟）
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_.is_open()) {
        log_file_ << now << "," << header.seq << ","
                  << (int)header.priority << "," << delay_ms << ","
                  << command << std::endl;
    }
}

} // namespace DataTransmit
