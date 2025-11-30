#pragma once

#include "sender.h"
#include <memory>
#include <atomic>

/**
 * SendCenter 类
 * 封装Sender，自动管理stream_id，简化发送接口
 */
class SendCenter {
 public:
  /**
   * 构造函数
   * @param server_addr 目标服务器地址
   * @param server_port 目标服务器端口
   * @param symbol_size RaptorQ符号大小（默认256字节）
   * @param encode_thread_count 编码线程数量（默认4）
   */
  SendCenter(const std::string& server_addr, uint16_t server_port, 
             uint16_t symbol_size = Sender::kDefaultSymbolSize, 
             uint32_t encode_thread_count = Sender::kDefaultEncodeThreadCount);
  ~SendCenter();

  /**
   * 启动发送器
   */
  void start();
  
  /**
   * 停止发送器
   */
  void stop();

  /**
   * 发送数据（自动分配stream_id）
   * @param data 要发送的数据
   * @return true 成功放入队列，false 失败
   */
  bool sendData(std::shared_ptr<std::string> data);
  
  /**
   * 设置修复符号比例
   * @param ratio 修复符号占源符号的比例（0.0 - 1.0）
   */
  void setRepairRatio(float ratio);
  
  /**
   * 获取统计信息
   */
  uint64_t getSentCount() const { return sender_.getSentCount(); }
  uint64_t getSentSymbolCount() const { return sender_.getSentSymbolCount(); }
  uint64_t getFailedCount() const { return sender_.getFailedCount(); }
  size_t getQueueSize() const { return sender_.getQueueSize(); }
  bool isRunning() const { return sender_.isRunning(); }

 private:
  Sender sender_;
  std::atomic<uint64_t> stream_id_counter_;
};