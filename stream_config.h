/**
 * StreamConfig - 流传输配置
 * 用于配置不同类型数据的传输参数
 */

#pragma once

#include <cstdint>
#include <string>
#include <iostream>

/**
 * 数据类型枚举
 */
enum class DataType : uint8_t {
    VIDEO = 0,      // 视频数据
    AUDIO = 1,      // 音频数据
    COMMAND = 2,    // 控制指令
    UNKNOWN = 255   // 未知类型
};

/**
 * 传输配置结构
 * 包含了RaptorQ编码、网络发送、队列缓冲的所有可调参数
 */
struct StreamConfig {
    // ========== 基础信息 ==========
    DataType data_type;         // 数据类型
    std::string name;           // 配置名称（用于日志）
    
    // ========== RaptorQ 编码参数 ==========
    uint16_t symbol_size;       // 符号大小（字节），建议：视频512，音频256，指令128
    float repair_ratio;         // 修复符号比例（0.0-1.0）
    uint32_t encode_threads;    // 编码线程数
    
    // ========== 网络发送参数 ==========
    uint32_t send_interval_us;  // 发送间隔（微秒），控制发包速率，0表示无限制
    uint32_t max_bandwidth_kbps; // 最大带宽限制（kbps），0表示无限制
    
    // ========== 队列缓冲参数 ==========
    size_t queue_size;          // 编码队列大小
    uint32_t max_pending_symbols; // 单个流最大待发送符号数（反压控制）
    
    // ========== 端口配置 ==========
    uint16_t bind_port;         // 本地绑定端口（Receiver用）
    uint16_t target_port;       // 目标端口（Sender用）
    
    // 构造函数，设置默认值
    StreamConfig()
        : data_type(DataType::UNKNOWN)
        , name("unknown")
        , symbol_size(256)
        , repair_ratio(0.1f)
        , encode_threads(4)
        , send_interval_us(0)
        , max_bandwidth_kbps(0)
        , queue_size(1000)
        , max_pending_symbols(10000)
        , bind_port(0)
        , target_port(0) {}
    
    // ========== 预置配置工厂方法 ==========
    
    /**
     * 视频传输配置
     * 高带宽、高冗余、允许一定延迟
     */
    static StreamConfig VideoConfig(uint16_t target_port = 9000) {
        StreamConfig config;
        config.data_type = DataType::VIDEO;
        config.name = "video";
        config.symbol_size = 512;           // 大符号，减少头部开销
        config.repair_ratio = 0.2f;         // 20% 冗余，抗丢包
        config.encode_threads = 4;          // 多线程编码
        config.send_interval_us = 100;      // 100us 间隔，约 10000 pkt/s
        config.max_bandwidth_kbps = 10000;  // 10 Mbps
        config.queue_size = 2000;           // 大队列，缓冲突发
        config.max_pending_symbols = 50000; // 允许大量待发送符号
        config.target_port = target_port;
        return config;
    }
    
    /**
     * 音频传输配置
     * 中等带宽、中等冗余、低延迟
     */
    static StreamConfig AudioConfig(uint16_t target_port = 9001) {
        StreamConfig config;
        config.data_type = DataType::AUDIO;
        config.name = "audio";
        config.symbol_size = 256;           // 中等符号
        config.repair_ratio = 0.15f;        // 15% 冗余
        config.encode_threads = 2;          // 中等线程数
        config.send_interval_us = 50;       // 50us 间隔，约 20000 pkt/s
        config.max_bandwidth_kbps = 500;    // 500 kbps
        config.queue_size = 500;            // 中等队列
        config.max_pending_symbols = 10000;
        config.target_port = target_port;
        return config;
    }
    
    /**
     * 指令传输配置
     * 低带宽、低冗余、超低延迟、高优先级
     */
    static StreamConfig CommandConfig(uint16_t target_port = 9002) {
        StreamConfig config;
        config.data_type = DataType::COMMAND;
        config.name = "command";
        config.symbol_size = 128;           // 小符号，快速编解码
        config.repair_ratio = 0.05f;        // 5% 冗余，指令通常不重传也可
        config.encode_threads = 1;          // 单线程，减少竞争
        config.send_interval_us = 0;        // 无间隔，立即发送
        config.max_bandwidth_kbps = 50;     // 50 kbps 足够
        config.queue_size = 100;            // 小队列，快速处理
        config.max_pending_symbols = 1000;  // 少量待发送
        config.target_port = target_port;
        return config;
    }
    
    /**
     * 打印配置信息（调试用）
     */
    void print() const {
        std::cout << "StreamConfig [" << name << "]:" << std::endl;
        std::cout << "  数据类型: " << static_cast<int>(data_type) << std::endl;
        std::cout << "  符号大小: " << symbol_size << " 字节" << std::endl;
        std::cout << "  修复比例: " << (repair_ratio * 100) << "%" << std::endl;
        std::cout << "  编码线程: " << encode_threads << std::endl;
        std::cout << "  发送间隔: " << send_interval_us << " us" << std::endl;
        std::cout << "  带宽限制: " << max_bandwidth_kbps << " kbps" << std::endl;
        std::cout << "  队列大小: " << queue_size << std::endl;
        std::cout << "  目标端口: " << target_port << std::endl;
    }
};
