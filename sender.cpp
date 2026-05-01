#include "sender.h"
#include "common.h"
#include <iostream>
#include <cstring>
#include <mutex>
#include <cstdlib>
#include <random>

Sender::Sender(const std::string& server_addr, uint16_t server_port, 
               uint16_t symbol_size, uint32_t encode_thread_count)
  : server_addr_(server_addr),
    server_port_(server_port),
    symbol_size_(symbol_size),
    repair_ratio_(0.1f),  // 默认 10% 修复符号
    encode_thread_count_(std::min(encode_thread_count, kMaxEncodeThreadCount)),
    client_(0),  // 自动分配本地端口
    running_(false),
    sent_count_(0),
    sent_symbol_count_(0),
    failed_count_(0),
    send_interval_us_(0),  // 默认无发送间隔限制
    queue_size_(kDefaultQueueSize),
    bypass_fec_(false),
    drop_rate_(0.0f),
    last_send_time_(std::chrono::steady_clock::now()) {
  // 设置客户端的默认目标地址
  client_.setDefaultTarget(server_addr_, server_port_);
  
  client_.setReceiveCallback(std::bind(&Sender::onReceive, this, std::placeholders::_1));
  client_.setErrorCallback(std::bind(&Sender::onError, this, std::placeholders::_1));
  
  // 为每个编码线程创建 EventLoop
  for (uint32_t i = 0; i < encode_thread_count_; ++i) {
    event_loops_.emplace_back(
        std::make_unique<EventBase::EventLoop>()
    );
    
    // 队列占位
    event_queues_.push_back(nullptr);
  }
}

Sender::~Sender() {
  stop();
}

void Sender::start() {
  if (running_) {
      return;
  }
  
  running_ = true;
  
  // 启动客户端接收（可选）
  client_.startReceiving();
  
  // 启动编码线程
  for (uint32_t i = 0; i < encode_thread_count_; ++i) {
      worker_threads_.emplace_back(&Sender::encodeLoopThread, this, i);
  }
  
  // 等待队列初始化完成
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  std::cout << "Sender: 已启动，目标: " << server_addr_ << ":" << server_port_ 
            << ", 符号大小: " << symbol_size_ << " 字节" << std::endl;
  std::cout << "Sender: 编码线程数: " << encode_thread_count_ << std::endl;
}

void Sender::stop() {
  if (!running_) {
      return;
  }
  
  running_ = false;
  
  // 停止所有编码队列
  for (auto& queue : event_queues_) {
    if (queue) {
        queue->Stop();
    }
  }
  
  // 停止所有编码 EventLoop
  for (auto& loop : event_loops_) {
    if (loop) {
        loop->Stop(true);
    }
  }
    
  // 停止接收
  client_.stopReceiving();
  
  // 等待所有编码线程结束
  for (auto& thread : worker_threads_) {
    if (thread.joinable()) {
        thread.join();
    }
  }
  
  worker_threads_.clear();
  event_queues_.clear();
  
  std::cout << "Sender: 已停止" << std::endl;
  std::cout << "  发送数据包: " << sent_count_ << std::endl;
  std::cout << "  发送符号数: " << sent_symbol_count_ << std::endl;
  std::cout << "  发送失败: " << failed_count_ << std::endl;
}

bool Sender::sendData(uint64_t stream_id, std::shared_ptr<std::string> data) {
  if (!running_) {
    std::cerr << "Sender: 未启动" << std::endl;
    return false;
  }
  
  // 基于流ID选择编码队列（确保同一流的数据在同一线程处理）
  uint64_t queue_idx = stream_id % encode_thread_count_;
  
  auto item = std::make_shared<DataItem>(stream_id, std::move(data));
  
  auto& queue = event_queues_[queue_idx];
  if (!queue || !queue->Push(std::move(item))) {
    std::cerr << "Sender: 编码队列 " << queue_idx << " 已满，丢弃数据包" << std::endl;
    failed_count_++;
    return false;
  }
  
  return true;
}

void Sender::setRepairRatio(float ratio) {
  if (ratio >= 0.0f && ratio <= 1.0f) {
    repair_ratio_ = ratio;
  }
}

float Sender::getRepairRatio() const {
  return repair_ratio_;
}

void Sender::setNextRepairRatio(uint64_t stream_id, float ratio) {
  if (ratio >= 0.0f && ratio <= 1.0f) {
    std::lock_guard<std::mutex> lock(next_repair_mutex_);
    next_repair_ratios_[stream_id] = ratio;
  }
}

void Sender::setBypassFec(bool enable) {
  bypass_fec_ = enable;
  std::cout << "Sender: Bypass FEC " << (enable ? "启用" : "禁用") << std::endl;
}

void Sender::setDropRate(float rate) {
  if (rate >= 0.0f && rate <= 1.0f) {
    drop_rate_ = rate;
    std::cout << "Sender: 模拟丢包率设置为 " << (rate * 100.0f) << "%" << std::endl;
  }
}

void Sender::setSendInterval(uint32_t interval_us) {
  send_interval_us_ = interval_us;
  std::cout << "Sender: 发送间隔设置为 " << interval_us << " us" << std::endl;
}

void Sender::setFeedbackCallback(FeedbackCallback callback) {
  feedback_callback_ = callback;
}

void Sender::setQueueSize(size_t size) {
  queue_size_ = size;
  std::cout << "Sender: 队列大小设置为 " << size << std::endl;
}

void Sender::SetLogFile(const std::string& path) {
  std::lock_guard<std::mutex> lock(log_mutex_);
  if (log_file_.is_open()) {
    log_file_.close();
  }
  log_file_.open(path, std::ios::app);
}

double Sender::getCurrentSendRate() const {
  std::lock_guard<std::mutex> lock(stat_mutex_);
  
  auto now = std::chrono::steady_clock::now();
  // 清理1秒前的记录
  size_t valid_count = 0;
  for (auto it = send_times_.begin(); it != send_times_.end(); ) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - *it).count();
    if (elapsed > 1000) {
      it = send_times_.erase(it);
    } else {
      ++it;
      ++valid_count;
    }
  }
  
  return valid_count;  // 过去1秒内的发送次数
}

size_t Sender::getQueueSize() const {
  size_t total = 0;
  for (const auto& queue : event_queues_) {
    if (queue) {
      total += queue->Size();
    }
  }
  return total;
}

void Sender::onReceive(std::shared_ptr<Network::Packet> packet) {
  // 检查是否是反馈包
  if (packet->data.size() == FeedbackPacket::kSerializedSize) {
    FeedbackPacket feedback;
    if (FeedbackPacket::deserialize(packet->data.data(), packet->data.size(), feedback)) {
      if (feedback_callback_) {
        feedback_callback_(feedback);
      }
      return;
    }
  }
  
  // 接收到服务器响应
  std::cout << "Sender: 收到响应 " << packet->data.size() << " 字节, 来自 "
            << packet->remote_addr << ":" << packet->remote_port << std::endl;
}

void Sender::onError(const std::string& error) {
  std::cerr << "Sender 错误: " << error << std::endl;
}

void Sender::encodeLoopThread(uint32_t thread_id) {
  std::cout << "Sender: 编码线程 " << thread_id << " 启动" << std::endl;
  
  // 获取当前线程的 EventLoop
  auto& loop = event_loops_[thread_id];
  
  // 在当前线程中创建编码队列
  try {
    event_queues_[thread_id] = std::make_unique<EventBase::EventQueue<std::shared_ptr<DataItem>>>(
      loop->GetEventBase(),
      [this, thread_id](std::shared_ptr<DataItem>& item) {
        try {
          encodeAndSendPacket(thread_id, item);
        } catch (const std::exception& e) {
          std::cerr << "Sender: 编码线程 " << thread_id << " 处理异常: " 
                    << e.what() << std::endl;
          failed_count_++;
        }
      },
      queue_size_
    );
    
    std::cout << "Sender: 编码线程 " << thread_id << " 队列已创建" << std::endl;
      
  } catch (const std::exception& e) {
    std::cerr << "Sender: 编码线程 " << thread_id << " 创建队列失败: " 
              << e.what() << std::endl;
    return;
  }
  
  // 运行 EventLoop（阻塞）
  std::cout << "Sender: 编码线程 " << thread_id << " 开始运行 EventLoop" << std::endl;
  loop->Run();
  
  std::cout << "Sender: 编码线程 " << thread_id << " 退出" << std::endl;
}

void Sender::encodeAndSendPacket(uint32_t thread_id, std::shared_ptr<DataItem>& item) {
  uint32_t stream_id = item->stream_id;
  auto& data = item->data;
  
  // Bypass 模式：直接发送原始数据，不经过 RaptorQ 编码
  if (bypass_fec_) {
    BypassPacketHeader header;
    header.stream_id = stream_id;
    header.data_length = static_cast<uint32_t>(data->size());
    
    std::vector<uint8_t> packet_data(sizeof(BypassPacketHeader) + data->size());
    std::memcpy(packet_data.data(), &header, sizeof(BypassPacketHeader));
    std::memcpy(packet_data.data() + sizeof(BypassPacketHeader), 
                data->data(), data->size());
    
    // 发送间隔控制
    if (send_interval_us_ > 0) {
      std::lock_guard<std::mutex> lock(send_time_mutex_);
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_send_time_).count();
      if (elapsed < send_interval_us_) {
        std::this_thread::sleep_for(std::chrono::microseconds(send_interval_us_ - elapsed));
      }
    }
    
    // 模拟网络丢包
    if (false && drop_rate_ > 0.0f && ((float)std::rand() / RAND_MAX) < drop_rate_) {
      return; // 模拟丢包：不发送
    }
    
    ssize_t sent = client_.send(packet_data);
    
    if (sent > 0) {
      sent_count_++;
      if (stream_id % 30 == 0) {
        std::cout << "[Sender Bypass] 发送成功，流 " << stream_id 
                  << ", 大小: " << packet_data.size() << " 字节" << std::endl;
      }
      if (send_interval_us_ > 0) {
        std::lock_guard<std::mutex> lock(stat_mutex_);
        send_times_.push_back(std::chrono::steady_clock::now());
        if (send_times_.size() > 10000) {
          send_times_.erase(send_times_.begin());
        }
      }
    } else {
      failed_count_++;
      std::cerr << "[Sender Bypass] 发送失败，流 " << stream_id 
                << ", 大小: " << packet_data.size() 
                << ", 返回值: " << sent << std::endl;
    }
    
    if (send_interval_us_ > 0) {
      std::lock_guard<std::mutex> lock(send_time_mutex_);
      last_send_time_ = std::chrono::steady_clock::now();
    }
    
    return;
  }
  
  // 1. 创建 RaptorQ 编码器
  RQPack::Encoder encoder(
    reinterpret_cast<const uint8_t*>(data->data()), 
    data->size(), 
    symbol_size_
  );
  
  if (!encoder.isReady()) {
    std::cerr << "[编码线程 " << thread_id << "] 编码器初始化失败" << std::endl;
    failed_count_++;
    return;
  }
  
  // 2. 计算修复符号数量（支持临时覆盖 + 概率精细化）
  uint32_t source_symbols = encoder.getSourceSymbolCount();
  float effective_ratio = repair_ratio_;
  {
    std::lock_guard<std::mutex> lock(next_repair_mutex_);
    auto it = next_repair_ratios_.find(stream_id);
    if (it != next_repair_ratios_.end()) {
      effective_ratio = it->second;
      next_repair_ratios_.erase(it);
    }
  }
  
  // 概率化修复符号数：小数部分 = 额外多发 1 个修复符号的概率
  // 这样 10 源符号 × 25% = 2.5 → 50% 概率发 3 个，长期期望 2.5 个
  float exact_repair = source_symbols * effective_ratio;
  uint32_t base_repair = static_cast<uint32_t>(exact_repair);
  float fractional = exact_repair - base_repair;
  
  uint32_t repair_count = base_repair;
  if (fractional > 0.0f) {
    // 线程局部延迟初始化，避免每次调用都构造 std::random_device
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(gen) < fractional) {
      repair_count++;
    }
  }
  
  // (日志已关闭以减少终端输出)
  // std::cout << "[编码线程 " << thread_id << "] 流 " << stream_id 
  //           << " 编码数据 " << data->size() << " 字节, "
  //           << "源符号: " << source_symbols << ", "
  //           << "目标冗余: " << (effective_ratio * 100) << "%, "
  //           << "精确修复: " << exact_repair << ", "
  //           << "实际修复: " << repair_count << std::endl;
  
  // 3. 生成所有符号（源符号 + 修复符号）
  std::vector<RQPack::Symbol> symbols;
  try {
    symbols = encoder.encodeAll(repair_count);
  } catch (const std::exception& e) {
    std::cerr << "[编码线程 " << thread_id << "] 编码失败: " << e.what() << std::endl;
    failed_count_++;
    return;
  }
  
  // 4. 准备发送参数
  uint32_t original_size = data->size();
  uint32_t total_symbols = symbols.size();
  
  // (日志已关闭以减少终端输出)
  // std::cout << "[编码线程 " << thread_id << "] 流 " << stream_id 
  //           << " 编码完成，符号数: " << total_symbols << "，开始发送..." << std::endl;
  
  // 5. 直接发送每个符号
  uint32_t sent_count = 0;
  uint32_t failed_count = 0;
  
  for (const auto& symbol : symbols) {
    // 构造数据包：头部 + 符号数据
    PacketHeader header;
    header.stream_id = stream_id;
    header.symbol_id = symbol.id;
    header.total_symbols = total_symbols;
    header.original_size = original_size;
    header.symbol_size = symbol_size_;
    header.reserved = 0;
    
    // 组装完整数据包
    std::vector<uint8_t> packet_data;
    packet_data.resize(sizeof(PacketHeader) + symbol.data.size());
    
    // 复制头部
    std::memcpy(packet_data.data(), &header, sizeof(PacketHeader));
    
    // 复制符号数据
    std::memcpy(packet_data.data() + sizeof(PacketHeader), 
                symbol.data.data(), 
                symbol.data.size());
    
    // 发送间隔控制
    if (send_interval_us_ > 0) {
      std::lock_guard<std::mutex> lock(send_time_mutex_);
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_send_time_).count();
      if (elapsed < send_interval_us_) {
        std::this_thread::sleep_for(std::chrono::microseconds(send_interval_us_ - elapsed));
      }
    }
    
    // 模拟网络丢包
    if (false && drop_rate_ > 0.0f && ((float)std::rand() / RAND_MAX) < drop_rate_) {
      continue; // 模拟丢包：跳过此符号
    }
    
    // 发送
    ssize_t sent = client_.send(packet_data);
    
    if (sent > 0) {
      sent_count++;
      sent_symbol_count_++;
      // 记录发送时间
      {
        std::lock_guard<std::mutex> lock(stat_mutex_);
        send_times_.push_back(std::chrono::steady_clock::now());
        // 限制统计队列大小
        if (send_times_.size() > 10000) {
          send_times_.erase(send_times_.begin());
        }
      }
    } else {
      failed_count++;
      this->failed_count_++;
      std::cerr << "[编码线程 " << thread_id << "] 发送符号 " << symbol.id << " 失败" << std::endl;
    }
    
    // 更新最后发送时间
    if (send_interval_us_ > 0) {
      std::lock_guard<std::mutex> lock(send_time_mutex_);
      last_send_time_ = std::chrono::steady_clock::now();
    }
  }
  
  // 6. 统计
  sent_count_++;
  
  // (日志已关闭以减少终端输出)
  // std::cout << "[编码线程 " << thread_id << "] 流 " << stream_id << " 发送完成, "
  //           << "成功: " << sent_count << "/" << total_symbols 
  //           << ", 失败: " << failed_count << std::endl;
}

bool Sender::sendSymbols(uint64_t stream_id, const std::vector<RQPack::Symbol>& symbols,
                         uint32_t original_size, uint16_t symbol_size) {
  if (!running_) {
    std::cerr << "Sender: 未启动" << std::endl;
    return false;
  }
  
  {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_.is_open()) {
      log_file_ << "[Sender::sendSymbols] stream_id=" << stream_id 
                << ", symbols=" << symbols.size() 
                << ", original_size=" << original_size << std::endl;
    }
  }
  
  // 直接使用client_发送（避免进入编码队列再次编码）
  uint32_t total_symbols = symbols.size();
  uint32_t sent_count = 0;
  
  for (const auto& symbol : symbols) {
    // 构造数据包：头部 + 符号数据
    PacketHeader header;
    header.stream_id = stream_id;
    header.symbol_id = symbol.id;
    header.total_symbols = total_symbols;
    header.original_size = original_size;
    header.symbol_size = symbol_size;
    header.reserved = 0;
    
    // 组装完整数据包
    std::vector<uint8_t> packet_data;
    packet_data.resize(sizeof(PacketHeader) + symbol.data.size());
    
    // 复制头部
    std::memcpy(packet_data.data(), &header, sizeof(PacketHeader));
    
    // 复制符号数据
    std::memcpy(packet_data.data() + sizeof(PacketHeader), 
                symbol.data.data(), 
                symbol.data.size());
    
    // 发送间隔控制
    if (send_interval_us_ > 0) {
      std::lock_guard<std::mutex> lock(send_time_mutex_);
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_send_time_).count();
      if (elapsed < send_interval_us_) {
        std::this_thread::sleep_for(std::chrono::microseconds(send_interval_us_ - elapsed));
      }
    }
    
    // 模拟网络丢包
    if (false && drop_rate_ > 0.0f && ((float)std::rand() / RAND_MAX) < drop_rate_) {
      continue; // 模拟丢包：跳过此符号
    }
    
    // 直接发送
    ssize_t sent = client_.send(packet_data);
    
    if (sent > 0) {
      sent_count++;
      sent_symbol_count_++;
      if (symbol.id % 10 == 0) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (log_file_.is_open()) {
          log_file_ << "[Sender::sendSymbols] Sent symbol " << symbol.id << "/" << total_symbols 
                    << " for stream " << stream_id << std::endl;
        }
      }
      // 记录发送时间
      {
        std::lock_guard<std::mutex> lock(stat_mutex_);
        send_times_.push_back(std::chrono::steady_clock::now());
        if (send_times_.size() > 10000) {
          send_times_.erase(send_times_.begin());
        }
      }
    } else {
      failed_count_++;
    }
    
    // 更新最后发送时间
    if (send_interval_us_ > 0) {
      std::lock_guard<std::mutex> lock(send_time_mutex_);
      last_send_time_ = std::chrono::steady_clock::now();
    }
  }
  
  sent_count_++;
  
  {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_.is_open()) {
      log_file_ << "[Sender::sendSymbols] Finished: sent " << sent_count << "/" << total_symbols 
                << " symbols for stream " << stream_id << std::endl;
    }
  }
  
  return sent_count > 0;
}
