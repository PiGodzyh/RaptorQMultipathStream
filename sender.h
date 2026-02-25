#pragma once

#include <thread>
#include <vector>
#include <memory>
#include <atomic>
#include <string>
#include <chrono>

#include "network/network_client.h"
#include "event_base/event_queue.h"
#include "event_base/event_loop.h"
#include "pack/rq_pack.h"

/**
 * Sender 类
 * 基于 EventLoop 的多线程异步数据包发送器
 * 使用多个编码线程进行 RaptorQ 编码，编码完成后直接发送
 */
class Sender {
 public:
  static const size_t kDefaultQueueSize = 1000;
  static const uint16_t kDefaultSymbolSize = 256;
  static const uint32_t kDefaultEncodeThreadCount = 4;
  static const uint32_t kMaxEncodeThreadCount = 16;
    
 public:
  /**
   * 构造函数
   * @param server_addr 目标服务器地址
   * @param server_port 目标服务器端口
   * @param symbol_size RaptorQ 符号大小（默认 256 字节）
   * @param encode_thread_count 编码线程数量（默认 4）
   */
  Sender(const std::string& server_addr, uint16_t server_port, 
         uint16_t symbol_size = kDefaultSymbolSize,
         uint32_t encode_thread_count = kDefaultEncodeThreadCount);
  ~Sender();
    
  /**
   * 启动发送器
   */
  void start();
  
  /**
   * 停止发送器
   */
  void stop();
  
  /**
   * 发送数据（异步，非阻塞）
   * 数据会被 RaptorQ 编码后发送
   * @param stream_id 流ID（用于标识数据流，由调用方确认唯一id）
   * @param data 要发送的数据
   * @return true 成功放入队列，false 队列满或已停止
   */
  bool sendData(uint64_t stream_id, std::shared_ptr<std::string> data);
    
  /**
   * 设置 RaptorQ 修复符号比例
   * @param ratio 修复符号占源符号的比例（0.0 - 1.0），默认 0.1 (10%)
   */
  void setRepairRatio(float ratio);
  
  /**
   * 设置发送间隔（微秒）
   * 用于控制发包速率，0 表示无限制
   * @param interval_us 发送间隔（微秒）
   */
  void setSendInterval(uint32_t interval_us);
  
  /**
   * 设置编码队列大小
   * @param size 队列大小
   */
  void setQueueSize(size_t size);
  
  /**
   * 获取当前发送速率（pkt/s）
   */
  double getCurrentSendRate() const;
  
  /**
   * 获取已发送的数据包总数（原始数据包，非符号）
   */
  uint64_t getSentCount() const { return sent_count_; }
    
  /**
   * 获取已发送的符号总数
   */
  uint64_t getSentSymbolCount() const { return sent_symbol_count_; }
  
  /**
   * 获取发送失败的符号总数
   */
  uint64_t getFailedCount() const { return failed_count_; }
  
  /**
   * 获取编码队列总大小
   */
  size_t getQueueSize() const;
    
  /**
   * 检查是否正在运行
   */
  bool isRunning() const { return running_; }
    
 protected:
  /**
   * 网络接收回调（接收服务器响应）
   */
  void onReceive(std::shared_ptr<Network::Packet> packet);
  
  /**
   * 错误回调
   */
  void onError(const std::string& error);
  
  /**
   * 编码线程函数（每个线程运行自己的 EventLoop 和编码队列）
   */
  void encodeLoopThread(uint32_t thread_id);
    
  /**
   * 数据项结构，包含流ID和数据
   */
  struct DataItem {
    uint64_t stream_id;
    std::shared_ptr<std::string> data;
    
    DataItem(uint64_t sid, std::shared_ptr<std::string> d)
      : stream_id(sid), data(std::move(d)) {}
  };
  
  /**
   * 处理数据包编码和发送（在编码线程的 EventLoop 中调用）
   * 使用 RaptorQ 编码，编码完成后直接发送
   */
  void encodeAndSendPacket(uint32_t thread_id, std::shared_ptr<DataItem>& item);
  
  /**
   * 选择一个编码队列（轮询策略）
   */
  uint32_t selectQueue();

 protected:
  std::string server_addr_;
  uint16_t server_port_;
  uint16_t symbol_size_;
  float repair_ratio_;
  uint32_t encode_thread_count_;
  
  Network::UDPClient client_;
  
  // 编码线程相关（多个线程，每个线程有自己的 EventLoop 和队列）
  std::vector<std::unique_ptr<EventBase::EventLoop>> event_loops_;
  std::vector<std::unique_ptr<EventBase::EventQueue<std::shared_ptr<DataItem>>>> event_queues_;
  std::vector<std::thread> worker_threads_;
  
  std::atomic<bool> running_;
  
  std::atomic<uint64_t> sent_count_;         // 发送的数据包数
  std::atomic<uint64_t> sent_symbol_count_;  // 发送的符号数
  std::atomic<uint64_t> failed_count_;       // 发送失败的符号数
  
  // 新增可配置参数
  uint32_t send_interval_us_;                // 发送间隔（微秒）
  size_t queue_size_;                        // 队列大小
  
  // 发送速率统计
  mutable std::mutex stat_mutex_;
  mutable std::vector<std::chrono::steady_clock::time_point> send_times_;
  std::chrono::steady_clock::time_point last_send_time_;
  mutable std::mutex send_time_mutex_;
};
