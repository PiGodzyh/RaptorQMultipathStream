/*
 * RaptorQ 打包/解包封装接口 (C++)
 * 提供简单易用的 FEC 编码和解码功能
 */

#ifndef RQ_PACK_HPP
#define RQ_PACK_HPP

#include <vector>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace RQPack {

/**
 * 编码符号结构
 */
struct Symbol {
    uint32_t id;                    // 符号ID (ESI)
    std::vector<uint8_t> data;      // 符号数据
    
    Symbol() : id(0) {}
    Symbol(uint32_t _id, std::vector<uint8_t> _data) 
        : id(_id), data(std::move(_data)) {}
};

/**
 * RaptorQ 编码器
 * 用于对原始数据进行 FEC 编码，生成源符号和修复符号
 */
class Encoder {
public:
    /**
     * 构造函数
     * 
     * @param data: 要编码的原始数据
     * @param symbol_size: 每个符号的大小（字节），建议 8-1024，默认 256
     * @throw std::runtime_error: 如果初始化失败
     */
    Encoder(const std::vector<uint8_t>& data, uint16_t symbol_size = 256);
    
    /**
     * 构造函数（从原始指针）
     * 
     * @param data: 要编码的原始数据指针
     * @param data_size: 数据大小（字节）
     * @param symbol_size: 每个符号的大小（字节），建议 8-1024，默认 256
     * @throw std::runtime_error: 如果初始化失败
     */
    Encoder(const uint8_t* data, size_t data_size, uint16_t symbol_size = 256);
    
    ~Encoder();
    
    // 禁止拷贝
    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    
    // 支持移动
    Encoder(Encoder&& other) noexcept;
    Encoder& operator=(Encoder&& other) noexcept;
    
    /**
     * 获取源符号数量
     */
    uint32_t getSourceSymbolCount() const;
    
    /**
     * 获取最大修复符号数量
     */
    uint32_t getMaxRepairSymbolCount() const;
    
    /**
     * 获取符号大小
     */
    uint16_t getSymbolSize() const;
    
    /**
     * 编码生成符号
     * 
     * @param symbol_id: 符号ID（0 ~ sourceSymbols-1 为源符号，之后为修复符号）
     * @return: 编码的符号
     * @throw std::runtime_error: 如果编码失败
     */
    Symbol encode(uint32_t symbol_id);
    
    /**
     * 批量生成符号（包括源符号和修复符号）
     * 
     * @param repair_count: 额外生成的修复符号数量，默认为源符号数量的 10%
     * @return: 符号列表
     */
    std::vector<Symbol> encodeAll(uint32_t repair_count = 0);
    
    /**
     * 检查编码器是否已准备好
     */
    bool isReady() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * RaptorQ 解码器
 * 用于从接收到的符号中恢复原始数据
 */
class Decoder {
public:
    /**
     * 构造函数
     * 
     * @param data_size: 期望解码的数据大小（字节）
     * @param symbol_size: 符号大小（字节），必须与编码器一致
     * @throw std::runtime_error: 如果初始化失败
     */
    Decoder(size_t data_size, uint16_t symbol_size);
    
    ~Decoder();
    
    // 禁止拷贝
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    
    // 支持移动
    Decoder(Decoder&& other) noexcept;
    Decoder& operator=(Decoder&& other) noexcept;
    
    /**
     * 添加接收到的符号
     * 
     * @param symbol: 接收到的符号
     * @return: 添加是否成功
     */
    bool addSymbol(const Symbol& symbol);
    
    /**
     * 批量添加符号
     * 
     * @param symbols: 符号列表
     * @return: 成功添加的符号数量
     */
    size_t addSymbols(const std::vector<Symbol>& symbols);
    
    /**
     * 检查是否可以解码
     */
    bool canDecode() const;
    
    /**
     * 获取还需要多少个符号才能解码
     */
    uint16_t neededSymbols() const;
    
    /**
     * 执行解码
     * 
     * @return: 解码后的原始数据
     * @throw std::runtime_error: 如果解码失败
     */
    std::vector<uint8_t> decode();
    
    /**
     * 解码到指定缓冲区
     * 
     * @param out_data: 输出缓冲区
     * @param out_size: 缓冲区大小
     * @return: 实际解码的字节数
     * @throw std::runtime_error: 如果解码失败
     */
    size_t decodeTo(uint8_t* out_data, size_t out_size);
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

/**
 * 工具函数：一键编码
 * 
 * @param data: 原始数据
 * @param symbol_size: 符号大小
 * @param repair_ratio: 修复符号比例（0.0-1.0），默认 0.1 即 10%
 * @return: 编码后的符号列表
 */
std::vector<Symbol> quickEncode(const std::vector<uint8_t>& data, 
                                 uint16_t symbol_size = 256, 
                                 float repair_ratio = 0.1f);

/**
 * 工具函数：一键解码
 * 
 * @param symbols: 接收到的符号列表
 * @param data_size: 原始数据大小
 * @param symbol_size: 符号大小
 * @return: 解码后的原始数据
 */
std::vector<uint8_t> quickDecode(const std::vector<Symbol>& symbols, 
                                  size_t data_size, 
                                  uint16_t symbol_size);

} // namespace RQPack

#endif /* RQ_PACK_HPP */

