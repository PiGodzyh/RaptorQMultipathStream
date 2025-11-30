# RQPack - RaptorQ 简易封装库

这是对 libRaptorQ 库的 C++ 封装，提供了简单易用的接口来进行前向纠错编码（FEC）。

## 功能特性

- ✅ 简洁的 C++ 接口，符合现代 C++ 风格
- ✅ 自动管理内存，使用 RAII 和智能指针
- ✅ 异常处理，错误信息清晰
- ✅ 支持批量操作
- ✅ 提供快速编码/解码接口

## 编译

### 前提条件

1. 确保 libRaptorQ 已经编译
   ```bash
   cd ../libRaptorQ/build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make -j4
   ```

2. 编译 RQPack

   **方法一：使用 Makefile（推荐）**
   ```bash
   make
   ```

   **方法二：使用构建脚本**
   ```bash
   ./build.sh
   ```

### 运行测试

```bash
# 运行简单示例
build/example

# 或使用 make
make run-example

# 运行完整测试
build/test_pack

# 或使用 make
make test
```

### 清理

```bash
make clean
```

**注意**: 所有编译生成的文件都在 `build/` 目录中，包括：
- `build/librqpack.a` - 静态库
- `build/example` - 示例程序
- `build/test_pack` - 测试程序
- `build/*.o` - 目标文件

## 使用示例

### 基本编码解码

```cpp
#include "rq_pack.h"
#include <vector>
#include <iostream>

using namespace RQPack;

int main() {
    // 1. 准备原始数据
    std::vector<uint8_t> original_data = {1, 2, 3, 4, 5, 6, 7, 8};
    
    // 2. 创建编码器并编码
    Encoder encoder(original_data, 64);  // 符号大小 64 字节
    
    // 获取信息
    std::cout << "源符号数量: " << encoder.getSourceSymbolCount() << std::endl;
    std::cout << "最大修复符号数量: " << encoder.getMaxRepairSymbolCount() << std::endl;
    
    // 生成所有符号（源符号 + 修复符号）
    auto symbols = encoder.encodeAll(2);  // 额外生成 2 个修复符号
    
    // 3. 创建解码器并解码
    Decoder decoder(original_data.size(), 64);
    
    // 添加符号
    decoder.addSymbols(symbols);
    
    // 检查是否可以解码
    if (decoder.canDecode()) {
        auto decoded_data = decoder.decode();
        
        // 验证
        if (original_data == decoded_data) {
            std::cout << "解码成功！" << std::endl;
        }
    }
    
    return 0;
}
```

### 快速接口

```cpp
#include "rq_pack.h"

using namespace RQPack;

int main() {
    std::vector<uint8_t> data = {/* 你的数据 */};
    
    // 一键编码（生成 15% 冗余）
    auto symbols = quickEncode(data, 128, 0.15f);
    
    // 一键解码
    auto decoded = quickDecode(symbols, data.size(), 128);
    
    return 0;
}
```

### 模拟丢包场景

```cpp
#include "rq_pack.h"
#include <random>

using namespace RQPack;

int main() {
    std::vector<uint8_t> data(1024);
    // ... 填充数据 ...
    
    // 编码
    Encoder encoder(data, 128);
    auto all_symbols = encoder.encodeAll(10);  // 生成 10 个额外修复符号
    
    // 模拟随机丢包（丢失 20%）
    std::vector<Symbol> received_symbols;
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    for (const auto& symbol : all_symbols) {
        if (dist(rng) > 0.2f) {  // 保留 80%
            received_symbols.push_back(symbol);
        }
    }
    
    // 尝试解码
    Decoder decoder(data.size(), 128);
    decoder.addSymbols(received_symbols);
    
    std::cout << "还需要: " << decoder.neededSymbols() << " 个符号" << std::endl;
    
    if (decoder.canDecode()) {
        auto decoded = decoder.decode();
        std::cout << "解码成功！" << std::endl;
    }
    
    return 0;
}
```

## API 参考

### Encoder 类

#### 构造函数
- `Encoder(const std::vector<uint8_t>& data, uint16_t symbol_size = 256)`
- `Encoder(const uint8_t* data, size_t data_size, uint16_t symbol_size = 256)`

#### 方法
- `uint32_t getSourceSymbolCount() const` - 获取源符号数量
- `uint32_t getMaxRepairSymbolCount() const` - 获取最大修复符号数量
- `uint16_t getSymbolSize() const` - 获取符号大小
- `Symbol encode(uint32_t symbol_id)` - 编码单个符号
- `std::vector<Symbol> encodeAll(uint32_t repair_count = 0)` - 批量生成符号
- `bool isReady() const` - 检查是否就绪

### Decoder 类

#### 构造函数
- `Decoder(size_t data_size, uint16_t symbol_size)`

#### 方法
- `bool addSymbol(const Symbol& symbol)` - 添加一个符号
- `size_t addSymbols(const std::vector<Symbol>& symbols)` - 批量添加符号
- `bool canDecode() const` - 检查是否可以解码
- `uint16_t neededSymbols() const` - 获取还需要的符号数量
- `std::vector<uint8_t> decode()` - 解码返回数据
- `size_t decodeTo(uint8_t* out_data, size_t out_size)` - 解码到指定缓冲区

### Symbol 结构

```cpp
struct Symbol {
    uint32_t id;                    // 符号ID
    std::vector<uint8_t> data;      // 符号数据
};
```

### 工具函数

- `std::vector<Symbol> quickEncode(const std::vector<uint8_t>& data, uint16_t symbol_size = 256, float repair_ratio = 0.1f)`
- `std::vector<uint8_t> quickDecode(const std::vector<Symbol>& symbols, size_t data_size, uint16_t symbol_size)`

## 参数建议

### symbol_size（符号大小）
- 范围：8 - 1024 字节
- 建议值：
  - 小数据（< 10KB）：64 - 128 字节
  - 中等数据（10KB - 1MB）：128 - 512 字节
  - 大数据（> 1MB）：512 - 1024 字节

### repair_ratio（修复符号比例）
- 范围：0.0 - 1.0
- 建议值：
  - 低丢包率（< 5%）：0.05 - 0.1
  - 中等丢包率（5% - 15%）：0.15 - 0.25
  - 高丢包率（> 15%）：0.3 - 0.5

## 错误处理

所有可能失败的操作都会抛出 `std::runtime_error` 异常，建议使用 try-catch 捕获：

```cpp
try {
    Encoder encoder(data, 128);
    auto symbols = encoder.encodeAll();
    // ...
} catch (const std::runtime_error& e) {
    std::cerr << "错误: " << e.what() << std::endl;
}
```

## 性能提示

1. 符号大小建议设置为 2 的幂次（64, 128, 256, 512）
2. 数据量越大，编码计算时间越长
3. 修复符号数量不宜过多（通常不超过源符号的 50%）
4. 解码器接收到足够的符号后立即解码以节省内存

## 许可证

本封装库遵循与 libRaptorQ 相同的 LGPL v3 许可证。

