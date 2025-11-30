#include "receiver_center.h"
#include <iostream>

ReceiverCenter::ReceiverCenter(uint16_t port, uint32_t thread_count)
  : receiver_(this, port, thread_count),
    event_loop_(std::make_unique<EventBase::EventLoop>()),
    event_queue_(nullptr),
    running_(false),
    msg_callback_(nullptr),
    next_expected_stream_id_(0),
    max_priority_queue_size_(kDefaultMaxPriorityQueueSize) {
  std::cout << "ReceiverCenter: 已创建 (端口: " << port 
            << ", Receiver线程数: " << thread_count << ")" << std::endl;
}

ReceiverCenter::~ReceiverCenter() {
  stop();
}

void ReceiverCenter::start() {
  if (running_) {
    return;
  }
  
  running_ = true;
  
  // 启动处理线程
  worker_thread_ = std::thread(&ReceiverCenter::processThread, this);
  
  // 等待队列初始化完成
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  std::cout << "ReceiverCenter: 处理线程已启动" << std::endl;
  
  // 启动Receiver（阻塞，所以这应该在最后）
  std::cout << "ReceiverCenter: 启动Receiver..." << std::endl;
  receiver_.start();
}

void ReceiverCenter::stop() {
  if (!running_) {
    return;
  }
  
  running_ = false;
  
  // 停止Receiver
  receiver_.stop();
  
  // 停止EventQueue
  if (event_queue_) {
    event_queue_->Stop();
  }
  
  // 停止EventLoop
  if (event_loop_) {
    event_loop_->Stop(true);
  }
  
  // 等待处理线程结束
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
  
  std::cout << "ReceiverCenter: 已停止" << std::endl;
}

void ReceiverCenter::setMsgCallback(MsgCallback callback) {
  msg_callback_ = callback;
}

void ReceiverCenter::OnDecodeComplete(uint32_t stream_id, const std::vector<uint8_t>& data) {
  // 从Receiver接收解码完成的回调
  // 将数据项放入EventQueue进行排队处理
  
  if (!event_queue_) {
    std::cerr << "ReceiverCenter: EventQueue未初始化，丢弃数据 (流ID: " 
              << stream_id << ")" << std::endl;
    return;
  }
  
  auto item = std::make_shared<DecodedItem>(stream_id, data);
  
  if (!event_queue_->Push(std::move(item))) {
    std::cerr << "ReceiverCenter: EventQueue已满，丢弃数据 (流ID: " 
              << stream_id << ")" << std::endl;
  }
}

void ReceiverCenter::processThread() {
  std::cout << "ReceiverCenter: 处理线程启动" << std::endl;
  
  // 在当前线程中创建EventQueue
  try {
    event_queue_ = std::make_unique<EventBase::EventQueue<std::shared_ptr<DecodedItem>>>(
      event_loop_->GetEventBase(),
      [this](std::shared_ptr<DecodedItem>& item) {
        try {
          processDecodedItem(item);
        } catch (const std::exception& e) {
          std::cerr << "ReceiverCenter: 处理异常: " << e.what() << std::endl;
        }
      },
      kDefaultQueueSize
    );
    
    std::cout << "ReceiverCenter: EventQueue已创建" << std::endl;
    
  } catch (const std::exception& e) {
    std::cerr << "ReceiverCenter: 创建EventQueue失败: " << e.what() << std::endl;
    return;
  }
  
  // 运行EventLoop（阻塞）
  std::cout << "ReceiverCenter: 开始运行EventLoop" << std::endl;
  event_loop_->Run();
  
  std::cout << "ReceiverCenter: 处理线程退出" << std::endl;
}

void ReceiverCenter::processDecodedItem(std::shared_ptr<DecodedItem>& item) {
  uint32_t stream_id = item->stream_id;
  
  // 如果是期望的下一个stream_id，直接处理
  if (stream_id == next_expected_stream_id_) {
    // 回调用户
    std::cout << "ReceiverCenter: 按序处理流 " << stream_id 
              << " (大小: " << item->data.size() << " 字节)" << std::endl;
    
    if (msg_callback_) {
      msg_callback_(item->data);
    }
    
    // 更新期望的下一个stream_id
    next_expected_stream_id_++;
    
    HandlePendingItems();
  } else if (stream_id > next_expected_stream_id_) {
    // 如果stream_id比期望的大，说明乱序到达，放入最小堆
    std::cout << "ReceiverCenter: 流 " << stream_id << " 乱序到达"
              << " (期望: " << next_expected_stream_id_ 
              << ", 缓存大小: " << pending_items_.size() + 1 << ")" << std::endl;
    
    pending_items_.push(item);
    
    if (pending_items_.size() >= max_priority_queue_size_) {
      const auto& pending_item = pending_items_.top();
      std::cout << "ReceiverCenter: 缓存满， " << next_expected_stream_id_ << " to " << pending_item->stream_id << "丢包 " << std::endl;
      next_expected_stream_id_ = pending_item->stream_id;
      HandlePendingItems();
    }
  } else {
    // stream_id < next_expected_stream_id_，说明是重复或过时的包，丢弃
    std::cout << "ReceiverCenter: 丢弃过时的流 " << stream_id 
              << " (期望: " << next_expected_stream_id_ << ")" << std::endl;
  }
}

void ReceiverCenter::HandlePendingItems() {
  while (!pending_items_.empty() && pending_items_.top()->stream_id == next_expected_stream_id_) {
    const auto& pending_item = pending_items_.top();
    
    std::cout << "ReceiverCenter: 从缓存处理流 " << pending_item->stream_id 
              << " (大小: " << pending_item->data.size() << " 字节)" << std::endl;
    
    if (msg_callback_) {
      msg_callback_(pending_item->data);
    }
    
    next_expected_stream_id_++;
    pending_items_.pop();
  }
  
  // 打印缓存状态
  if (!pending_items_.empty()) {
    std::cout << "ReceiverCenter: 缓存中还有 " << pending_items_.size() 
              << " 个数据包，最小stream_id: " << pending_items_.top()->stream_id 
              << ", 期望: " << next_expected_stream_id_ << std::endl;
  }
}

