#pragma once

#include <cstdint>

// 数据包头部结构
struct PacketHeader {
  uint64_t stream_id;         // 数据流ID
  uint32_t symbol_id;         // 符号ID
  uint32_t total_symbols;     // 总符号数（源符号 + 修复符号）
  uint32_t original_size;     // 原始数据大小（字节）
  uint16_t symbol_size;       // 符号大小
  uint16_t reserved;          // 保留字段（对齐）
} __attribute__((packed));
