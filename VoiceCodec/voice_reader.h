/**
 * voice_reader.h - 音频文件读取器
 * 
 * 支持读取 WAV/PCM 音频文件，提取音频帧用于传输
 */

#ifndef VOICE_READER_H
#define VOICE_READER_H

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <functional>
#include "voice_codec.h"

namespace VoiceCodec {

// WAV 文件头部结构
struct WavHeader {
    char riff[4];           // "RIFF"
    uint32_t file_size;     // 文件大小-8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmt_size;      // fmt chunk 大小 (16)
    uint16_t audio_format;  // 音频格式 (1=PCM)
    uint16_t channels;      // 声道数
    uint32_t sample_rate;   // 采样率
    uint32_t byte_rate;     // 字节率
    uint16_t block_align;   // 块对齐
    uint16_t bits_per_sample; // 位深
    char data[4];           // "data"
    uint32_t data_size;     // 音频数据大小
};

// 音频帧
struct VoiceFrame {
    uint32_t seq;                       // 帧序号
    uint64_t timestamp_us;              // 时间戳
    std::vector<uint8_t> pcm_data;      // PCM 数据
    bool is_valid;                      // 是否有效
};

/**
 * 音频文件读取器
 */
class VoiceReader {
public:
    VoiceReader();
    ~VoiceReader();
    
    /**
     * 打开音频文件
     * @param filepath 文件路径（支持 WAV/PCM）
     * @return 是否成功
     */
    bool Open(const std::string& filepath);
    
    /**
     * 关闭文件
     */
    void Close();
    
    /**
     * 读取一帧音频数据
     * @return 音频帧
     */
    VoiceFrame ReadFrame();
    
    /**
     * 是否已打开
     */
    bool IsOpen() const { return file_.is_open(); }
    
    /**
     * 获取音频参数
     */
    uint32_t GetSampleRate() const { return sample_rate_; }
    uint16_t GetChannels() const { return channels_; }
    uint16_t GetBitsPerSample() const { return bits_per_sample_; }
    uint32_t GetTotalFrames() const { return total_frames_; }
    
    /**
     * 获取当前帧位置
     */
    uint32_t GetCurrentFrame() const { return current_frame_; }
    
    /**
     * 是否已读到文件末尾
     */
    bool IsEndOfFile() const { return current_frame_ >= total_frames_; }

private:
    std::ifstream file_;
    std::string filepath_;
    
    // 音频参数
    uint32_t sample_rate_ = 8000;
    uint16_t channels_ = 1;
    uint16_t bits_per_sample_ = 16;
    uint32_t data_offset_ = 0;      // 音频数据起始偏移
    uint32_t data_size_ = 0;        // 音频数据大小
    
    // 帧管理
    uint32_t frame_size_ = 0;       // 每帧字节数
    uint32_t total_frames_ = 0;     // 总帧数
    uint32_t current_frame_ = 0;    // 当前帧序号
    
    // 解析 WAV 头部
    bool ParseWavHeader();
    
    // 解析 PCM 文件（无头部）
    void SetupPcmParams();
};

/**
 * 音频文件写入器
 */
class VoiceWriter {
public:
    VoiceWriter();
    ~VoiceWriter();
    
    /**
     * 创建输出文件
     * @param filepath 输出路径
     * @param sample_rate 采样率
     * @param channels 声道数
     * @param bits_per_sample 位深
     * @return 是否成功
     */
    bool Create(const std::string& filepath, 
                uint32_t sample_rate = 8000,
                uint16_t channels = 1,
                uint16_t bits_per_sample = 16);
    
    /**
     * 关闭文件并写入 WAV 头部
     */
    void Close();
    
    /**
     * 写入音频帧
     * @param frame 音频帧
     * @return 是否成功
     */
    bool WriteFrame(const VoiceFrame& frame);
    
    /**
     * 写入原始 PCM 数据
     * @param pcm_data PCM 数据
     * @return 是否成功
     */
    bool WritePcm(const std::vector<uint8_t>& pcm_data);
    
    /**
     * 是否已打开
     */
    bool IsOpen() const { return file_.is_open(); }
    
    /**
     * 获取已写入帧数
     */
    uint32_t GetWrittenFrames() const { return written_frames_; }

private:
    std::ofstream file_;
    std::string filepath_;
    
    // 音频参数
    uint32_t sample_rate_ = 8000;
    uint16_t channels_ = 1;
    uint16_t bits_per_sample_ = 16;
    uint32_t frame_size_ = 0;
    
    // 写入统计
    uint32_t written_frames_ = 0;
    uint32_t written_bytes_ = 0;
    size_t header_position_ = 0;    // WAV 头部位置（稍后更新）
    
    // 写入 WAV 头部（临时，稍后更新）
    void WriteWavHeader();
    
    // 更新 WAV 头部（文件关闭时调用）
    void UpdateWavHeader();
};

} // namespace VoiceCodec

#endif // VOICE_READER_H
