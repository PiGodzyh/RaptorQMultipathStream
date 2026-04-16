#pragma once

#include <thread>
#include <vector>
#include <memory>
#include <atomic>
#include <map>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <set>

#include "network/network_server.h"
#include "event_base/event_queue.h"
#include "event_base/event_loop.h"
#include "pack/rq_pack.h"

// 流解码器状态
struct StreamDecoder {
  uint32_t stream_id;
  size_t expected_data_size;
  uint16_t symbol_size;
  uint32_t total_symbols;
  std::unique_ptr<RQPack::Decoder> decoder;
  std::set<uint32_t> received_symbols;  // 已接收的符号ID
  bool decoded;
  
  StreamDecoder(uint32_t id, size_t data_size, uint16_t sym_size, uint32_t total)
      : stream_id(id),
        expected_data_size(data_size),
        symbol_size(sym_size),
        total_symbols(total),
        decoder(std::make_unique<RQPack::Decoder>(data_size, sym_size)),
        decoded(false) {}
};

// 解码完成回调
using DecodeCompleteCallback = std::function<void(uint32_t stream_id, const std::vector<uint8_t>& data)>;

class Receiver {
 public:
  class Visitor {
    public:
    virtual void OnDecodeComplete(uint32_t stream_id, const std::vector<uint8_t>& data) = 0;
  };
 public:
  static const uint32_t kMaxThreadCount = 10;
  static const size_t kDefaultQueueSize = 1000;
    
 public:
  /**
   * 构造函数
   * @param port 监听端口
   * @param thread_count 工作线程数量（每个线程一个队列和一个EventLoop）
   */
  Receiver(Visitor *visitor, uint16_t port, uint32_t thread_count);
  ~Receiver();
    
  void start();
  void stop();

  /**
   * 获取接收到的数据包总数
   */
  uint64_t getReceivedCount() const { return received_count_; }
    
  /**
   * 获取已处理的数据包总数
   */
  uint64_t getProcessedCount() const { return processed_count_; }
    
  /**
   * 获取已完成解码的流数量
   */
  uint64_t getDecodedStreamCount() const { return decoded_stream_count_; }
    
 protected:
  /**
   * 网络接收回调
   */
  void onReceive(std::shared_ptr<Network::Packet> packet);
    
  /**
   * 错误回调
   */
  void onError(const std::string& error);
    
  /**
   * 工作线程函数（运行 EventLoop）
   * @param thread_id 线程ID（0 到 thread_count-1）
   */
  void eventLoopThread(uint32_t thread_id);
  
  /**
   * 处理数据包（在工作线程的 EventLoop 中调用）
   * 解析 RaptorQ 符号并解码
   */
  virtual void processPacket(uint32_t thread_id, std::shared_ptr<Network::Packet>& packet);
  
  /**
   * 选择队列索引（可以重写实现不同的负载均衡策略）
   * 默认使用轮询（Round-Robin）
   */
  virtual uint32_t selectQueue(std::shared_ptr<Network::Packet> packet);
  
  /**
   * 获取或创建流解码器
   */
  StreamDecoder* getOrCreateDecoder(uint32_t thread_id, uint32_t stream_id, 
                                    size_t data_size, uint16_t symbol_size, 
                                    uint32_t total_symbols);

 protected:
  Visitor *visitor_;
  Network::UDPServer server_;
  uint32_t thread_count_;
  
  // 每个线程一个队列
  std::vector<std::unique_ptr<EventBase::EventQueue<std::shared_ptr<Network::Packet>>>> event_queues_;
  
  // 每个线程一个 EventLoop
  std::vector<std::unique_ptr<EventBase::EventLoop>> event_loops_;
  
  // 每个线程一个解码器映射表（stream_id -> StreamDecoder）
  std::vector<std::map<uint32_t, std::unique_ptr<StreamDecoder>>> stream_decoders_;
  
  std::vector<std::thread> worker_threads_;
    
  std::atomic<bool> running_;
  // 队列初始化同步
  std::atomic<uint32_t> initialized_queues_{0};
  std::mutex init_mutex_;
  std::condition_variable init_cv_;
  // 统计信息，实际使用时可以删除
  std::atomic<uint64_t> received_count_;
  std::atomic<uint64_t> processed_count_;
  std::atomic<uint64_t> decoded_stream_count_;
};
