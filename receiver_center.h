#pragma once

#include "receiver.h"
#include "event_base/event_queue.h"
#include "event_base/event_loop.h"
#include <memory>
#include <thread>
#include <functional>
#include <atomic>
#include <queue>
#include <map>

/**
 * 解码完成的数据项
 */
struct DecodedItem {
  uint32_t stream_id;
  std::vector<uint8_t> data;
  
  DecodedItem(uint32_t sid, const std::vector<uint8_t>& d)
    : stream_id(sid), data(d) {}
  
  DecodedItem(uint32_t sid, std::vector<uint8_t>&& d)
    : stream_id(sid), data(std::move(d)) {}
};

/**
 * 用于最小堆的比较器（比较shared_ptr中的stream_id）
 */
struct DecodedItemComparator {
  bool operator()(const std::shared_ptr<DecodedItem>& a, 
                  const std::shared_ptr<DecodedItem>& b) const {
    // 返回true表示a的优先级低于b（a > b），实现最小堆
    return a->stream_id > b->stream_id;
  }
};

/**
 * ReceiverCenter 类
 * 接收Receiver的解码回调，按stream_id顺序处理并回调
 */
class ReceiverCenter : private Receiver::Visitor {
 public:
  // 用户回调类型
  using MsgCallback = std::function<void(const std::vector<uint8_t>& data)>;
  
  static const size_t kDefaultQueueSize = 1000;
  static const uint32_t kDefaultMaxPriorityQueueSize = 20;
  
 public:
  /**
   * 构造函数
   * @param port 监听端口
   * @param thread_count Receiver工作线程数量
   */
  ReceiverCenter(uint16_t port, uint32_t thread_count = 4);
  ~ReceiverCenter();
  
  /**
   * 启动
   */
  void start();
  
  /**
   * 停止
   */
  void stop();
  
  /**
   * 设置用户回调函数
   * @param callback
   */
  void setMsgCallback(MsgCallback callback);
  
  /**
   * 获取接收器的统计信息,实际使用时可以删除这些接口
   */
  uint64_t getReceivedCount() const { return receiver_.getReceivedCount(); }
  uint64_t getProcessedCount() const { return receiver_.getProcessedCount(); }
  uint64_t getDecodedStreamCount() const { return receiver_.getDecodedStreamCount(); }
  size_t getQueueSize() const { return event_queue_ ? event_queue_->Size() : 0; }
  
  /**
   * 检查是否正在运行
   */
  bool isRunning() const { return running_; }

 private:
  /**
   * Receiver::Visitor 接口实现
   * 接收Receiver的解码完成回调
   */
  void OnDecodeComplete(uint32_t stream_id, const std::vector<uint8_t>& data) override;
  
  /**
   * 处理线程函数
   */
  void processThread();
  
  /**
   * 处理解码完成的数据项
   */
  void processDecodedItem(std::shared_ptr<DecodedItem>& item);

  /**
   * 处理等待回调的stream
   */
  void HandlePendingItems();

  // TODO: 定时器

 private:
  Receiver receiver_;
  std::unique_ptr<EventBase::EventLoop> event_loop_;
  std::unique_ptr<EventBase::EventQueue<std::shared_ptr<DecodedItem>>> event_queue_;
  std::thread worker_thread_;
  
  std::atomic<bool> running_;
  MsgCallback msg_callback_;
  
  // 按顺序处理相关
  uint64_t next_expected_stream_id_;  // 期望的下一个stream_id
  uint32_t max_priority_queue_size_; // 最大堆大小，即满堆认为丢包，不再等待
  std::priority_queue<std::shared_ptr<DecodedItem>, 
                      std::vector<std::shared_ptr<DecodedItem>>, 
                      DecodedItemComparator> pending_items_;  // 最小堆
};