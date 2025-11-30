/*
 * RQPack 简单使用示例
 */

#include "rq_pack.h"
#include <iostream>
#include <string>

using namespace RQPack;
using namespace std;

int main() {
    cout << "=== RQPack 简单示例 ===" << endl << endl;
    
    try {
        // 1. 准备要传输的数据
        string message = "Hello, RaptorQ! 这是一个简单的测试消息。";
        vector<uint8_t> data(message.begin(), message.end());
        
        cout << "原始消息: " << message << endl;
        cout << "数据大小: " << data.size() << " 字节" << endl << endl;
        
        // 2. 编码：生成符号
        cout << "--- 编码阶段 ---" << endl;
        Encoder encoder(data, 64);  // 使用 64 字节符号大小
        
        cout << "源符号数量: " << encoder.getSourceSymbolCount() << endl;
        cout << "符号大小: " << encoder.getSymbolSize() << " 字节" << endl;
        
        // 生成所有符号，包括 2 个额外的修复符号
        auto symbols = encoder.encodeAll(2);
        cout << "总共生成: " << symbols.size() << " 个符号" << endl << endl;
        
        // 3. 模拟网络传输：打印每个符号的信息
        cout << "--- 传输符号 ---" << endl;
        for (size_t i = 0; i < min(size_t(5), symbols.size()); ++i) {
            cout << "符号 " << symbols[i].id << ": " 
                 << symbols[i].data.size() << " 字节" << endl;
        }
        if (symbols.size() > 5) {
            cout << "... (还有 " << (symbols.size() - 5) << " 个符号)" << endl;
        }
        cout << endl;
        
        // 4. 解码：从符号恢复数据
        cout << "--- 解码阶段 ---" << endl;
        Decoder decoder(data.size(), encoder.getSymbolSize());
        
        // 添加所有符号
        size_t added = decoder.addSymbols(symbols);
        cout << "添加了 " << added << " 个符号" << endl;
        cout << "还需要 " << decoder.neededSymbols() << " 个符号才能解码" << endl;
        cout << "可以解码: " << (decoder.canDecode() ? "是" : "否") << endl << endl;
        
        // 执行解码
        if (decoder.canDecode()) {
            auto decoded_data = decoder.decode();
            string decoded_message(decoded_data.begin(), decoded_data.end());
            
            cout << "解码消息: " << decoded_message << endl;
            cout << "解码数据大小: " << decoded_data.size() << " 字节" << endl << endl;
            
            // 验证
            if (data == decoded_data) {
                cout << "✓ 验证成功：解码数据与原始数据一致！" << endl;
            } else {
                cout << "✗ 验证失败：解码数据与原始数据不一致！" << endl;
            }
        } else {
            cout << "✗ 无法解码：符号不足" << endl;
        }
        
    } catch (const exception& e) {
        cerr << "错误: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}

