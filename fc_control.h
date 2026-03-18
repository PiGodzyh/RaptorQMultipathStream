/**
 * fc_control.h - 飞控指令传输模块
 * 
 * 实现飞控指令的实时传输：
 * - 发送端：终端实时输入，每条指令一个 Source Block
 * - 接收端：收到后立即打印到终端
 * 
 * FEC策略：50%冗余，50ms超时重传
 */

#ifndef FC_CONTROL_H
#define FC_CONTROL_H

#include "data_common.h"
#include "sender.h"
#include "receiver.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <fstream>

namespace DataTransmit {

// ============================================================================
// 飞控指令发送器
// ============================================================================
class FCControlTransmitter {
public:
    FCControlTransmitter(const std::string& server_addr, int server_port);
    ~FCControlTransmitter();
    
    // 启动发送（阻塞，读取终端输入直到输入 "quit"）
    void Run();
    
    // 停止发送
    void Stop();
    
    // 发送单条指令（用于程序化调用）
    bool SendCommand(const std::string& command, uint8_t priority = FCControlHeader::PRIORITY_NORMAL);

private:
    // 发送单帧
    bool SendFrame(const FCControlPacket& packet, uint32_t seq);
    
    std::unique_ptr<Sender> sender_;
    std::atomic<uint32_t> seq_counter_{0};
    std::atomic<bool> running_{false};
    std::thread input_thread_;
    std::mutex send_mutex_;
    std::string server_addr_;
    int server_port_;
};

// ============================================================================
// 飞控指令接收器
// ============================================================================
class FCControlReceiver : public Receiver::Visitor {
public:
    FCControlReceiver(int listen_port);
    ~FCControlReceiver();
    
    // 启动接收
    void Start();
    
    // 停止接收
    void Stop();
    
    // 设置日志文件（可选）
    void SetLogFile(const std::string& log_path);

private:
    // Receiver::Visitor 回调
    void OnDecodeComplete(uint32_t stream_id, const std::vector<uint8_t>& data) override;
    
    std::unique_ptr<Receiver> receiver_;
    std::atomic<bool> running_{false};
    std::ofstream log_file_;
    std::mutex log_mutex_;
    uint32_t received_count_ = 0;
};

} // namespace DataTransmit

#endif // FC_CONTROL_H
