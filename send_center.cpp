#include "send_center.h"
#include <iostream>

SendCenter::SendCenter(const std::string& server_addr, uint16_t server_port, 
                       uint16_t symbol_size, uint32_t encode_thread_count)
  : sender_(server_addr, server_port, symbol_size, encode_thread_count),
    stream_id_counter_(0) {
  std::cout << "SendCenter: 已创建 (目标: " << server_addr << ":" << server_port 
            << ", 符号大小: " << symbol_size 
            << ", 编码线程: " << encode_thread_count << ")" << std::endl;
}

SendCenter::~SendCenter() {
  stop();
}

void SendCenter::start() {
  sender_.start();
  std::cout << "SendCenter: 已启动" << std::endl;
}

void SendCenter::stop() {
  sender_.stop();
  std::cout << "SendCenter: 已停止" << std::endl;
}

bool SendCenter::sendData(std::shared_ptr<std::string> data) {
  // 自动分配递增的stream_id
  uint64_t stream_id = stream_id_counter_.fetch_add(1);
  return sender_.sendData(stream_id, data);
}

void SendCenter::setRepairRatio(float ratio) {
  sender_.setRepairRatio(ratio);
}