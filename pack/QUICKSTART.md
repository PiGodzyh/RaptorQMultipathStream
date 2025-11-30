# RQPack 快速开始指南

## 简介

RQPack 是对 libRaptorQ 的 C++ 封装，提供简单易用的前向纠错编码（FEC）功能。

## 快速开始

### 1. 编译

```bash
# 确保你在 src_new/pack 目录下
make
```

### 2. 运行示例

```bash
# 运行简单示例
./example

# 运行完整测试
./test_pack
```

## 5 分钟教程

### 基本概念

- **编码器（Encoder）**: 将原始数据编码成多个符号（源符号 + 修复符号）
- **符号（Symbol）**: 编码后的数据块，包含 ID 和数据
- **解码器（Decoder）**: 从接收到的符号中恢复原始数据
- **源符号**: 原始数据分割成的符号
- **修复符号**: 额外生成的冗余符号，用于对抗丢包

### 最简单的使用方法

```cpp
#include "rq_pack.h"
using namespace RQPack;

// 原始数据
std::vector<uint8_t> data = {1, 2, 3, 4, 5};

// 一键编码（生成 10% 冗余）
auto symbols = quickEncode(data, 64, 0.1f);

// 一键解码
auto decoded = quickDecode(symbols, data.size(), 64);
```

### 详细使用步骤

#### 步骤 1: 创建编码器

```cpp
#include "rq_pack.h"
using namespace RQPack;

// 准备数据
std::vector<uint8_t> data(1024);
// ... 填充数据 ...

// 创建编码器（符号大小 128 字节）
Encoder encoder(data, 128);
```

#### 步骤 2: 生成符号

```cpp
// 方法一：生成所有符号（源符号 + 修复符号）
auto symbols = encoder.encodeAll(5);  // 额外生成 5 个修复符号

// 方法二：逐个生成符号
uint32_t source_count = encoder.getSourceSymbolCount();
for (uint32_t i = 0; i < source_count + 5; i++) {
    Symbol symbol = encoder.encode(i);
    // 发送 symbol...
}
```

#### 步骤 3: 传输符号

```cpp
// 符号可以通过网络、文件等方式传输
for (const auto& symbol : symbols) {
    // 传输 symbol.id 和 symbol.data
    sendOverNetwork(symbol.id, symbol.data);
}
```

#### 步骤 4: 创建解码器

```cpp
// 在接收端创建解码器（需要知道原始数据大小和符号大小）
Decoder decoder(1024, 128);
```

#### 步骤 5: 添加接收到的符号

```cpp
// 接收符号并添加到解码器
while (receivingSymbols) {
    Symbol symbol = receiveSymbol();
    decoder.addSymbol(symbol);
    
    // 检查是否可以解码
    if (decoder.canDecode()) {
        break;
    }
}
```

#### 步骤 6: 解码

```cpp
if (decoder.canDecode()) {
    auto decoded_data = decoder.decode();
    // 使用解码后的数据...
}
```

## 常见场景

### 场景 1: 文件传输（可能丢包）

```cpp
// 发送端
std::vector<uint8_t> file_data = readFile("data.bin");
Encoder encoder(file_data, 256);

// 生成 20% 冗余来对抗可能的丢包
uint32_t repair_count = encoder.getSourceSymbolCount() * 0.2;
auto symbols = encoder.encodeAll(repair_count);

// 发送所有符号
for (const auto& sym : symbols) {
    sendPacket(sym);
}

// 接收端
Decoder decoder(file_data.size(), 256);
while (receiving) {
    Symbol sym = receivePacket();
    decoder.addSymbol(sym);
    
    if (decoder.canDecode()) {
        auto data = decoder.decode();
        writeFile("received.bin", data);
        break;
    }
}
```

### 场景 2: 实时流传输

```cpp
// 发送端
const size_t CHUNK_SIZE = 4096;
std::vector<uint8_t> chunk(CHUNK_SIZE);

while (streaming) {
    readChunk(chunk);
    
    Encoder encoder(chunk, 128);
    auto symbols = encoder.encodeAll(3);  // 少量冗余
    
    for (const auto& sym : symbols) {
        sendUDP(sym);
    }
}

// 接收端
Decoder decoder(CHUNK_SIZE, 128);
while (receiving) {
    Symbol sym = receiveUDP();
    decoder.addSymbol(sym);
    
    if (decoder.canDecode()) {
        auto chunk = decoder.decode();
        playChunk(chunk);
        
        // 重置解码器准备下一块
        decoder = Decoder(CHUNK_SIZE, 128);
    }
}
```

## 参数调优建议

### symbol_size（符号大小）

| 数据大小 | 建议值 | 说明 |
|---------|--------|------|
| < 10KB | 64-128 | 小数据量，符号不宜过大 |
| 10KB - 100KB | 128-256 | 平衡编码速度和冗余 |
| 100KB - 1MB | 256-512 | 减少符号数量，提高效率 |
| > 1MB | 512-1024 | 大数据量，使用较大符号 |

### repair_ratio（冗余比例）

| 丢包率 | 建议冗余 | 说明 |
|--------|---------|------|
| < 5% | 10-15% | 低丢包环境 |
| 5-10% | 15-25% | 中等丢包环境 |
| 10-20% | 25-40% | 高丢包环境 |
| > 20% | 40-50% | 极端丢包环境 |

## 常见问题

### Q: 编译时找不到 libRaptorQ？

A: 确保先编译 libRaptorQ：
```bash
cd ../libRaptorQ/build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j4
```

### Q: 如何确定需要多少冗余？

A: 根据网络丢包率，一般设置为 `冗余 = 丢包率 + 5-10%` 的安全边际。

### Q: 解码器显示需要更多符号？

A: 使用 `decoder.neededSymbols()` 查看还需要多少符号，增加发送的修复符号数量。

### Q: 性能如何优化？

A: 
- 使用较大的符号大小（如 512 字节）
- 减少不必要的内存拷贝
- 批量处理符号
- 调整冗余比例

## 更多信息

查看完整文档：[README.md](README.md)

查看测试代码：[test_pack.cpp](test_pack.cpp)

查看示例代码：[example.cpp](example.cpp)

