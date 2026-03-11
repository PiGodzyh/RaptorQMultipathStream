#include "receiver.h"
#include <iostream>
#include <cstring>

#include "common.h"

Receiver::Receiver(Visitor *visitor, uint16_t port, uint32_t thread_count)
  : visitor_(visitor),
    server_(port),
    thread_count_(std::min(thread_count, kMaxThreadCount)),
    running_(false),
    received_count_(0),
    processed_count_(0),
    decoded_stream_count_(0) {
  server_.setReceiveCallback(std::bind(&Receiver::onReceive, this, std::placeholders::_1));
  server_.setErrorCallback(std::bind(&Receiver::onError, this, std::placeholders::_1));
  
  // 为每个线程创建 EventLoop 和资源
  for (uint32_t i = 0; i < thread_count_; ++i) {
    event_loops_.emplace_back(
        std::make_unique<EventBase::EventLoop>()
    );
    
    // 队列占位
    event_queues_.push_back(nullptr);
    
    // 解码器映射表
    stream_decoders_.emplace_back();
  }
}

Receiver::~Receiver() {
  stop();
}

void Receiver::start() {
  if (running_) {
      return;
  }
  
  running_ = true;
  
  // 启动工作线程，每个线程运行自己的 EventLoop
  for (uint32_t i = 0; i < thread_count_; ++i) {
      worker_threads_.emplace_back(&Receiver::eventLoopThread, this, i);
  }
    
  // 等待所有队列初始化完成
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // 启动网络服务器（非阻塞版本）
  server_.startAsync();
  
  std::cout << "Receiver: 已启动 " << thread_count_ 
            << " 个工作线程（每个线程一个队列和 EventLoop）" << std::endl;
}

void Receiver::stop() {
  if (!running_) {
      return;
  }
    
  running_ = false;
    
  // 停止网络服务器
  server_.stop();
  
  // 停止所有事件队列
  for (auto& queue : event_queues_) {
    if (queue) {
        queue->Stop();
    }
  }
    
  // 停止所有 EventLoop
  for (auto& loop : event_loops_) {
    if (loop) {
        loop->Stop(true);
    }
  }
  
  // 等待所有工作线程结束
  for (auto& thread : worker_threads_) {
    if (thread.joinable()) {
        thread.join();
    }
  }
    
  worker_threads_.clear();
  event_queues_.clear();
  
  std::cout << "Receiver: 已停止 (接收: " << received_count_ 
            << ", 处理: " << processed_count_ 
            << ", 解码完成: " << decoded_stream_count_ << ")" << std::endl;
}

void Receiver::onReceive(std::shared_ptr<Network::Packet> packet) {
  received_count_++;
  
  // 选择一个队列
  uint32_t queue_idx = selectQueue(packet);
  
  // 放入对应的队列（内部会自动通知 EventLoop）
  auto& queue = event_queues_[queue_idx];
  if (queue && !queue->Push(std::move(packet))) {
      std::cerr << "警告: 队列 " << queue_idx << " 已满，丢弃数据包" << std::endl;
  }
}

void Receiver::onError(const std::string& error) {
  std::cerr << "网络错误: " << error << std::endl;
}

void Receiver::eventLoopThread(uint32_t thread_id) {
  std::cout << "线程 " << thread_id << " 启动" << std::endl;
  
  // 获取当前线程的 EventLoop
  auto& loop = event_loops_[thread_id];
  
  // 在当前线程中创建队列（使用当前线程的 event_base）
  try {
    event_queues_[thread_id] = std::make_unique<EventBase::EventQueue<std::shared_ptr<Network::Packet>>>(
      loop->GetEventBase(),
      [this, thread_id](std::shared_ptr<Network::Packet>& packet) {
        try {
          processPacket(thread_id, packet);
          processed_count_++;
        } catch (const std::exception& e) {
          std::cerr << "线程 " << thread_id << " 处理异常: " 
                    << e.what() << std::endl;
        }
      },
      kDefaultQueueSize
    );
      
    std::cout << "线程 " << thread_id << " 队列已创建" << std::endl;
      
  } catch (const std::exception& e) {
    std::cerr << "线程 " << thread_id << " 创建队列失败: " << e.what() << std::endl;
    return;
  }
  
  // 运行 EventLoop（阻塞）
  std::cout << "线程 " << thread_id << " 开始运行 EventLoop" << std::endl;
  loop->Run();
  
  std::cout << "线程 " << thread_id << " EventLoop 退出" << std::endl;
}

void Receiver::processPacket(uint32_t thread_id, std::shared_ptr<Network::Packet>& packet) {
  // 1. 检查数据包大小
  if (packet->data.size() < sizeof(PacketHeader)) {
    std::cerr << "[线程 " << thread_id << "] 数据包太小: " << packet->data.size() << std::endl;
    return;
  }
  
  // 2. 解析头部
  PacketHeader header;
  std::memcpy(&header, packet->data.data(), sizeof(PacketHeader));
  
  // 3. 提取符号数据
  size_t symbol_data_offset = sizeof(PacketHeader);
  size_t symbol_data_size = packet->data.size() - symbol_data_offset;
  
  if (symbol_data_size > header.symbol_size) {
    std::cerr << "[线程 " << thread_id << "] 符号数据大小异常: " 
              << symbol_data_size << " > " << header.symbol_size << std::endl;
    return;
  }
  
  // 4. 使用头部中的原始数据大小
  size_t expected_data_size = header.original_size;
  
  // 5. 获取或创建流解码器
  StreamDecoder* stream_decoder = getOrCreateDecoder(
    thread_id, 
    header.stream_id,
    expected_data_size,
    header.symbol_size,
    header.total_symbols
  );
  
  if (!stream_decoder) {
    std::cerr << "[线程 " << thread_id << "] 创建解码器失败，流ID: " 
              << header.stream_id << std::endl;
    return;
  }
  
  // 6. 检查是否已经解码完成
  if (stream_decoder->decoded) {
    // 已经解码完成，忽略后续符号
    return;
  }
  
  // 7. 检查是否已经接收过这个符号
  if (stream_decoder->received_symbols.count(header.symbol_id) > 0) {
    // 重复符号，忽略
    return;
  }
  
  // 8. 构造符号并添加到解码器
  RQPack::Symbol symbol;
  symbol.id = header.symbol_id;
  symbol.data.assign(
    packet->data.begin() + symbol_data_offset,
    packet->data.begin() + symbol_data_offset + symbol_data_size
  );
  
  bool added = stream_decoder->decoder->addSymbol(symbol);
  
  if (added) {
    stream_decoder->received_symbols.insert(header.symbol_id);
    
    std::cout << "[线程 " << thread_id << "] 流 " << header.stream_id 
              << " 接收符号 #" << header.symbol_id 
              << " (" << stream_decoder->received_symbols.size() 
              << "/" << header.total_symbols << ")" << std::endl;
  }
  
  // 9. 检查是否可以解码
  if (stream_decoder->decoder->canDecode()) {
    std::cout << "[线程 " << thread_id << "] 流 " << header.stream_id 
              << " 可以解码，开始解码..." << std::endl;
    
    try {
      // 解码
      std::vector<uint8_t> decoded_data = stream_decoder->decoder->decode();
      
      stream_decoder->decoded = true;
      decoded_stream_count_++;
      
      std::cout << "[线程 " << thread_id << "] 流 " << header.stream_id 
                << " 解码成功！数据大小: " << decoded_data.size() << " 字节" << std::endl;
      
      // 调用解码完成回调
      if (visitor_) {
        visitor_->OnDecodeComplete(header.stream_id, decoded_data);
      }
    } catch (const std::exception& e) {
      std::cerr << "[线程 " << thread_id << "] 流 " << header.stream_id 
                << " 解码失败: " << e.what() << std::endl;
    }
  } else {
    uint16_t needed = stream_decoder->decoder->neededSymbols();
    std::cout << "[线程 " << thread_id << "] 流 " << header.stream_id 
              << " 还需要 " << needed << " 个符号" << std::endl;
  }
}

uint32_t Receiver::selectQueue(std::shared_ptr<Network::Packet> packet) {
  // 解析数据包头部获取流ID
  if (packet->data.size() < sizeof(PacketHeader)) {
    // 数据包太小，无法解析，使用轮询
    return received_count_.load() % thread_count_;
  }
  
  PacketHeader header;
  std::memcpy(&header, packet->data.data(), sizeof(PacketHeader));
  
  // 基于流ID哈希，确保同一流的所有包都在同一个线程处理
  return header.stream_id % thread_count_;
}

StreamDecoder* Receiver::getOrCreateDecoder(uint32_t thread_id, uint32_t stream_id,
                                            size_t data_size, uint16_t symbol_size,
                                            uint32_t total_symbols) {
  auto& decoders = stream_decoders_[thread_id];
  
  // 查找是否已存在
  auto it = decoders.find(stream_id);
  if (it != decoders.end()) {
      return it->second.get();
  }
  
  // 创建新的解码器
  try {
    auto decoder = std::make_unique<StreamDecoder>(
        stream_id, data_size, symbol_size, total_symbols
    );
    
    StreamDecoder* ptr = decoder.get();
    decoders[stream_id] = std::move(decoder);
    
    std::cout << "[线程 " << thread_id << "] 创建解码器，流ID: " << stream_id
              << ", 数据大小: " << data_size 
              << ", 符号大小: " << symbol_size
              << ", 总符号数: " << total_symbols << std::endl;
    
    return ptr;
      
  } catch (const std::exception& e) {
    std::cerr << "[线程 " << thread_id << "] 创建解码器失败: " << e.what() << std::endl;
    return nullptr;
  }
}
