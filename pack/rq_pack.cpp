/*
 * RaptorQ 打包/解包封装实现
 */

#include "rq_pack.h"
#include "RaptorQ/RaptorQ.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace RQPack {

// ============================================================================
// 辅助函数
// ============================================================================

// 根据数据大小和符号大小计算合适的 Block_Size
static RaptorQ_Block_Size calculateBlockSize(size_t data_size, uint16_t symbol_size) {
    size_t symbols = data_size / symbol_size;
    if (data_size % symbol_size != 0) {
        symbols++;
    }
    
    // 限制最大符号数量
    if (symbols > 56403) {
        symbols = 56403;  // RaptorQ 最大支持的符号数
    }
    
    static const size_t num_sizes = sizeof(RQ_blocks) / sizeof(RQ_blocks[0]);
    
    // 查找第一个大于等于所需符号数的块大小
    for (size_t i = 0; i < num_sizes; ++i) {
        if (static_cast<uint32_t>(RQ_blocks[i]) >= symbols) {
            return RQ_blocks[i];
        }
    }
    
    // 如果找不到，返回最大的块大小
    return RQ_Block_56403;
}

// ============================================================================
// Encoder::Impl
// ============================================================================

class Encoder::Impl {
public:
    Impl(const uint8_t* data, size_t data_size, uint16_t symbol_size)
        : data_(data, data + data_size)
        , symbol_size_(symbol_size)
        , api_(nullptr)
        , encoder_(nullptr)
    {
        // 获取 RaptorQ API
        api_ = RaptorQ_api(1);
        if (!api_) {
            throw std::runtime_error("无法获取 RaptorQ API");
        }
        
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        
        // 计算合适的块大小
        RaptorQ_Block_Size block_size = calculateBlockSize(data_size, symbol_size);
        
        // 创建编码器
        encoder_ = rq->Encoder(RQ_ENC_8, block_size, symbol_size);
        if (!encoder_ || !rq->initialized(encoder_)) {
            RaptorQ_free_api(&api_);
            throw std::runtime_error("无法初始化 RaptorQ 编码器");
        }
        
        // 设置数据
        uint8_t* data_ptr = const_cast<uint8_t*>(data_.data());
        void* ptr = static_cast<void*>(data_ptr);
        size_t written = rq->set_data(encoder_, &ptr, data_size);
        if (written != data_size) {
            rq->free(&encoder_);
            RaptorQ_free_api(&api_);
            throw std::runtime_error("设置编码数据失败");
        }
        
        // 同步计算编码
        if (!rq->compute_sync(encoder_)) {
            rq->free(&encoder_);
            RaptorQ_free_api(&api_);
            throw std::runtime_error("编码计算失败");
        }
    }
    
    ~Impl() {
        if (api_ && encoder_) {
            RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
            rq->free(&encoder_);
        }
        if (api_) {
            RaptorQ_free_api(&api_);
        }
    }
    
    uint32_t getSourceSymbolCount() const {
        if (!api_ || !encoder_) return 0;
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        return rq->symbols(encoder_);
    }
    
    uint32_t getMaxRepairSymbolCount() const {
        if (!api_ || !encoder_) return 0;
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        return rq->max_repair(encoder_);
    }
    
    uint16_t getSymbolSize() const {
        return symbol_size_;
    }
    
    Symbol encode(uint32_t symbol_id) {
        if (!api_ || !encoder_) {
            throw std::runtime_error("编码器未初始化");
        }
        
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        
        // 分配缓冲区
        std::vector<uint8_t> buffer(symbol_size_);
        uint8_t* ptr = buffer.data();
        void* void_ptr = static_cast<void*>(ptr);
        
        // 编码符号
        size_t written = rq->encode(encoder_, &void_ptr, symbol_size_, symbol_id);
        if (written == 0) {
            throw std::runtime_error("编码符号失败");
        }
        
        // 调整缓冲区大小到实际写入的数据量
        buffer.resize(written);
        
        return Symbol(symbol_id, std::move(buffer));
    }
    
    bool isReady() const {
        if (!api_ || !encoder_) return false;
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        return rq->ready(encoder_);
    }
    
private:
    std::vector<uint8_t> data_;
    uint16_t symbol_size_;
    RaptorQ_base_api* api_;
    RaptorQ_ptr* encoder_;
};

// ============================================================================
// Encoder 公共接口实现
// ============================================================================

Encoder::Encoder(const std::vector<uint8_t>& data, uint16_t symbol_size)
    : pImpl(std::make_unique<Impl>(data.data(), data.size(), symbol_size))
{
}

Encoder::Encoder(const uint8_t* data, size_t data_size, uint16_t symbol_size)
    : pImpl(std::make_unique<Impl>(data, data_size, symbol_size))
{
}

Encoder::~Encoder() = default;

Encoder::Encoder(Encoder&& other) noexcept = default;
Encoder& Encoder::operator=(Encoder&& other) noexcept = default;

uint32_t Encoder::getSourceSymbolCount() const {
    return pImpl->getSourceSymbolCount();
}

uint32_t Encoder::getMaxRepairSymbolCount() const {
    return pImpl->getMaxRepairSymbolCount();
}

uint16_t Encoder::getSymbolSize() const {
    return pImpl->getSymbolSize();
}

Symbol Encoder::encode(uint32_t symbol_id) {
    return pImpl->encode(symbol_id);
}

std::vector<Symbol> Encoder::encodeAll(uint32_t repair_count) {
    uint32_t source_count = getSourceSymbolCount();
    
    // 如果未指定修复符号数量，使用源符号数量的 10%
    if (repair_count == 0) {
        repair_count = std::max(1u, static_cast<uint32_t>(source_count * 0.1f));
    }
    
    std::vector<Symbol> symbols;
    symbols.reserve(source_count + repair_count);
    
    // 编码所有源符号
    for (uint32_t i = 0; i < source_count; ++i) {
        symbols.push_back(encode(i));
    }
    
    // 编码修复符号
    for (uint32_t i = 0; i < repair_count; ++i) {
        symbols.push_back(encode(source_count + i));
    }
    
    return symbols;
}

bool Encoder::isReady() const {
    return pImpl->isReady();
}

// ============================================================================
// Decoder::Impl
// ============================================================================

class Decoder::Impl {
public:
    Impl(size_t data_size, uint16_t symbol_size)
        : data_size_(data_size)
        , symbol_size_(symbol_size)
        , api_(nullptr)
        , decoder_(nullptr)
    {
        // 获取 RaptorQ API
        api_ = RaptorQ_api(1);
        if (!api_) {
            throw std::runtime_error("无法获取 RaptorQ API");
        }
        
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        
        // 计算合适的块大小
        RaptorQ_Block_Size block_size = calculateBlockSize(data_size, symbol_size);
        
        // 创建解码器
        decoder_ = rq->Decoder(RQ_DEC_8, block_size, symbol_size, RQ_COMPLETE);
        if (!decoder_ || !rq->initialized(decoder_)) {
            RaptorQ_free_api(&api_);
            throw std::runtime_error("无法初始化 RaptorQ 解码器");
        }
    }
    
    ~Impl() {
        if (api_ && decoder_) {
            RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
            rq->free(&decoder_);
        }
        if (api_) {
            RaptorQ_free_api(&api_);
        }
    }
    
    bool addSymbol(const Symbol& symbol) {
        if (!api_ || !decoder_) {
            return false;
        }
        
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        
        // 添加符号到解码器
        uint8_t* data_ptr = const_cast<uint8_t*>(symbol.data.data());
        void* ptr = static_cast<void*>(data_ptr);
        RaptorQ_Error err = rq->add_symbol(decoder_, &ptr, symbol.data.size(), symbol.id);
        
        return (err == RQ_ERR_NONE || err == RQ_ERR_NOT_NEEDED);
    }
    
    bool canDecode() const {
        if (!api_ || !decoder_) return false;
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        return rq->can_decode(decoder_);
    }
    
    uint16_t neededSymbols() const {
        if (!api_ || !decoder_) return 0;
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        return rq->needed_symbols(decoder_);
    }
    
    size_t decode(uint8_t* out_data, size_t out_size) {
        if (!api_ || !decoder_) {
            throw std::runtime_error("解码器未初始化");
        }
        
        RaptorQ_v1* rq = reinterpret_cast<RaptorQ_v1*>(api_);
        
        // 结束输入
        rq->end_of_input(decoder_, RQ_NO_FILL);
        
        // 等待解码完成
        RaptorQ_Dec_wait_res res = rq->wait_sync(decoder_);
        if (res.error != RQ_ERR_NONE) {
            throw std::runtime_error("解码等待失败");
        }
        
        // 解码数据
        void* ptr = static_cast<void*>(out_data);
        RaptorQ_Dec_Written written = rq->decode_bytes(decoder_, &ptr, out_size, 0, 0);
        
        if (written.written == 0) {
            throw std::runtime_error("解码失败");
        }
        
        return written.written;
    }
    
private:
    size_t data_size_;
    uint16_t symbol_size_;
    RaptorQ_base_api* api_;
    RaptorQ_ptr* decoder_;
    
public:
    size_t getDataSize() const { return data_size_; }
    uint16_t getSymbolSize() const { return symbol_size_; }
};

// ============================================================================
// Decoder 公共接口实现
// ============================================================================

Decoder::Decoder(size_t data_size, uint16_t symbol_size)
    : pImpl(std::make_unique<Impl>(data_size, symbol_size))
{
}

Decoder::~Decoder() = default;

Decoder::Decoder(Decoder&& other) noexcept = default;
Decoder& Decoder::operator=(Decoder&& other) noexcept = default;

bool Decoder::addSymbol(const Symbol& symbol) {
    return pImpl->addSymbol(symbol);
}

size_t Decoder::addSymbols(const std::vector<Symbol>& symbols) {
    size_t count = 0;
    for (const auto& symbol : symbols) {
        if (addSymbol(symbol)) {
            count++;
        }
    }
    return count;
}

bool Decoder::canDecode() const {
    return pImpl->canDecode();
}

uint16_t Decoder::neededSymbols() const {
    return pImpl->neededSymbols();
}

std::vector<uint8_t> Decoder::decode() {
    // 预估需要的缓冲区大小
    size_t data_size = pImpl->getDataSize();
    size_t symbol_size = pImpl->getSymbolSize();
    // 计算需要的符号数量
    size_t symbols_needed = (data_size + symbol_size - 1) / symbol_size;
    size_t buffer_size = symbols_needed * symbol_size;
    
    std::vector<uint8_t> buffer(buffer_size);
    
    size_t written = pImpl->decode(buffer.data(), buffer.size());
    
    // 调整为实际数据大小
    if (written > data_size) {
        buffer.resize(data_size);
    } else {
        buffer.resize(written);
    }
    
    return buffer;
}

size_t Decoder::decodeTo(uint8_t* out_data, size_t out_size) {
    return pImpl->decode(out_data, out_size);
}

// ============================================================================
// 工具函数
// ============================================================================

std::vector<Symbol> quickEncode(const std::vector<uint8_t>& data, 
                                 uint16_t symbol_size, 
                                 float repair_ratio)
{
    Encoder encoder(data, symbol_size);
    
    uint32_t source_count = encoder.getSourceSymbolCount();
    uint32_t repair_count = std::max(1u, 
                                     static_cast<uint32_t>(source_count * repair_ratio));
    
    return encoder.encodeAll(repair_count);
}

std::vector<uint8_t> quickDecode(const std::vector<Symbol>& symbols, 
                                  size_t data_size, 
                                  uint16_t symbol_size)
{
    Decoder decoder(data_size, symbol_size);
    
    decoder.addSymbols(symbols);
    
    if (!decoder.canDecode()) {
        throw std::runtime_error("符号不足，无法解码");
    }
    
    return decoder.decode();
}

} // namespace RQPack

