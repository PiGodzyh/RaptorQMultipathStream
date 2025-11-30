/*
 * RaptorQ 封装测试示例
 */

#include "rq_pack.h"
#include <iostream>
#include <random>
#include <iomanip>

using namespace RQPack;

// 辅助函数：打印数据（十六进制）
void printData(const std::vector<uint8_t>& data, size_t max_bytes = 32) {
    size_t print_size = std::min(data.size(), max_bytes);
    for (size_t i = 0; i < print_size; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(data[i]) << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    if (data.size() > max_bytes) {
        std::cout << "... (共 " << std::dec << data.size() << " 字节)";
    }
    std::cout << std::dec << std::endl;
}

// 基本编码解码测试
bool testBasicEncodeDecode() {
    std::cout << "\n========== 测试 1: 基本编码解码 ==========\n";
    
    try {
        // 创建测试数据
        std::vector<uint8_t> original_data(1024);
        std::mt19937 rng(12345);
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& byte : original_data) {
            byte = static_cast<uint8_t>(dist(rng));
        }
        
        std::cout << "原始数据大小: " << original_data.size() << " 字节\n";
        std::cout << "原始数据（前32字节）:\n";
        printData(original_data);
        
        // 编码
        std::cout << "\n开始编码...\n";
        Encoder encoder(original_data, 128);  // 符号大小 128 字节
        
        uint32_t source_count = encoder.getSourceSymbolCount();
        uint32_t max_repair = encoder.getMaxRepairSymbolCount();
        
        std::cout << "源符号数量: " << source_count << "\n";
        std::cout << "最大修复符号数量: " << max_repair << "\n";
        std::cout << "符号大小: " << encoder.getSymbolSize() << " 字节\n";
        
        // 生成所有符号（源符号 + 10% 修复符号）
        auto symbols = encoder.encodeAll(2);  // 额外生成 2 个修复符号
        std::cout << "总共生成符号数量: " << symbols.size() << "\n";
        
        // 解码
        std::cout << "\n开始解码...\n";
        Decoder decoder(original_data.size(), encoder.getSymbolSize());
        
        // 添加所有符号
        size_t added = decoder.addSymbols(symbols);
        std::cout << "成功添加符号数量: " << added << "\n";
        std::cout << "是否可以解码: " << (decoder.canDecode() ? "是" : "否") << "\n";
        
        // 执行解码
        auto decoded_data = decoder.decode();
        std::cout << "解码数据大小: " << decoded_data.size() << " 字节\n";
        std::cout << "解码数据（前32字节）:\n";
        printData(decoded_data);
        
        // 验证数据
        if (original_data == decoded_data) {
            std::cout << "\n✓ 测试通过：解码数据与原始数据完全一致！\n";
            return true;
        } else {
            std::cout << "\n✗ 测试失败：解码数据与原始数据不一致！\n";
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cout << "\n✗ 测试失败：" << e.what() << "\n";
        return false;
    }
}

// 测试丢包情况下的解码
bool testPacketLoss() {
    std::cout << "\n========== 测试 2: 丢包情况下的解码 ==========\n";
    
    try {
        // 创建测试数据
        std::vector<uint8_t> original_data(2048);
        std::mt19937 rng(54321);
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& byte : original_data) {
            byte = static_cast<uint8_t>(dist(rng));
        }
        
        std::cout << "原始数据大小: " << original_data.size() << " 字节\n";
        
        // 编码
        Encoder encoder(original_data, 256);
        uint32_t source_count = encoder.getSourceSymbolCount();
        
        std::cout << "源符号数量: " << source_count << "\n";
        
        // 生成符号（源符号 + 20% 修复符号）
        uint32_t repair_count = static_cast<uint32_t>(source_count * 0.2);
        auto all_symbols = encoder.encodeAll(repair_count);
        
        std::cout << "总共生成符号数量: " << all_symbols.size() 
                  << " (源: " << source_count << ", 修复: " << repair_count << ")\n";
        
        // 模拟 20% 丢包
        float loss_rate = 0.2f;
        std::vector<Symbol> received_symbols;
        std::uniform_real_distribution<float> loss_dist(0.0f, 1.0f);
        
        for (const auto& symbol : all_symbols) {
            if (loss_dist(rng) > loss_rate) {  // 保留 80% 的包
                received_symbols.push_back(symbol);
            }
        }
        
        std::cout << "模拟丢包率: " << (loss_rate * 100) << "%\n";
        std::cout << "实际接收符号数量: " << received_symbols.size() << "\n";
        
        // 解码
        Decoder decoder(original_data.size(), encoder.getSymbolSize());
        decoder.addSymbols(received_symbols);
        
        std::cout << "还需要符号数量: " << decoder.neededSymbols() << "\n";
        std::cout << "是否可以解码: " << (decoder.canDecode() ? "是" : "否") << "\n";
        
        if (!decoder.canDecode()) {
            std::cout << "\n✗ 测试失败：丢包太多，无法解码！\n";
            return false;
        }
        
        auto decoded_data = decoder.decode();
        
        // 验证数据
        if (original_data == decoded_data) {
            std::cout << "\n✓ 测试通过：即使有 " << (loss_rate * 100) 
                      << "% 丢包，仍然成功解码！\n";
            return true;
        } else {
            std::cout << "\n✗ 测试失败：解码数据与原始数据不一致！\n";
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cout << "\n✗ 测试失败：" << e.what() << "\n";
        return false;
    }
}

// 测试快速编码解码接口
bool testQuickAPI() {
    std::cout << "\n========== 测试 3: 快速编码解码接口 ==========\n";
    
    try {
        // 创建测试数据
        std::vector<uint8_t> original_data(512);
        for (size_t i = 0; i < original_data.size(); ++i) {
            original_data[i] = static_cast<uint8_t>(i & 0xFF);
        }
        
        std::cout << "原始数据大小: " << original_data.size() << " 字节\n";
        
        // 使用快速接口编码
        auto symbols = quickEncode(original_data, 64, 0.15f);  // 15% 冗余
        std::cout << "编码生成符号数量: " << symbols.size() << "\n";
        
        // 使用快速接口解码
        auto decoded_data = quickDecode(symbols, original_data.size(), 64);
        std::cout << "解码数据大小: " << decoded_data.size() << " 字节\n";
        
        // 验证数据
        if (original_data == decoded_data) {
            std::cout << "\n✓ 测试通过：快速接口工作正常！\n";
            return true;
        } else {
            std::cout << "\n✗ 测试失败：解码数据与原始数据不一致！\n";
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cout << "\n✗ 测试失败：" << e.what() << "\n";
        return false;
    }
}

int main() {
    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   RaptorQ 封装库测试                 ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    
    int passed = 0;
    int total = 3;
    
    if (testBasicEncodeDecode()) passed++;
    if (testPacketLoss()) passed++;
    if (testQuickAPI()) passed++;
    
    std::cout << "\n╔══════════════════════════════════════╗\n";
    std::cout << "║   测试结果: " << passed << "/" << total << " 通过";
    if (passed == total) {
        std::cout << "          ✓   ║\n";
    } else {
        std::cout << "          ✗   ║\n";
    }
    std::cout << "╚══════════════════════════════════════╝\n";
    
    return (passed == total) ? 0 : 1;
}

