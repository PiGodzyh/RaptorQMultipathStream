/**
 * voice_codec.h - 语音编解码模块
 * 
 * 使用 PortAudio 进行音频采集/播放
 * 支持 PCM 直接传输（测试模式）或 Opus 压缩
 */

#ifndef VOICE_CODEC_H
#define VOICE_CODEC_H

#include <vector>
#include <cstdint>
#include <functional>
#include <string>
#include <mutex>
#include <queue>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>

namespace VoiceCodec {

// 音频参数配置
constexpr int kSampleRate = 8000;           // 采样率：8kHz（语音足够）
constexpr int kChannels = 1;                // 声道数：单声道
constexpr int kBitsPerSample = 16;          // 位深：16bit
constexpr int kFrameDurationMs = 20;        // 帧时长：20ms
constexpr int kBytesPerFrame = kSampleRate * kFrameDurationMs / 1000 * kChannels * kBitsPerSample / 8;  // 320 bytes

// 音频帧结构
struct AudioFrame {
    uint32_t seq;                           // 帧序号
    uint64_t timestamp_us;                  // 时间戳（微秒）
    std::vector<uint8_t> data;              // PCM/压缩数据
};

// 音频采集回调类型
using AudioCaptureCallback = std::function<void(const AudioFrame& frame)>;

/**
 * 音频采集类
 * 使用 PortAudio 从麦克风采集音频
 */
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    
    /**
     * 初始化音频设备
     * @param device 设备名（"default" 或 "hw:0,0"）
     * @return 是否成功
     */
    bool Initialize(const std::string& device = "default");
    
    /**
     * 开始采集
     * @param callback 每帧采集完成后的回调
     */
    void Start(AudioCaptureCallback callback);
    
    /**
     * 停止采集
     */
    void Stop();
    
    /**
     * 读取一帧音频数据
     * @return 音频帧数据
     */
    std::vector<uint8_t> ReadFrame();
    
private:
    bool initialized_ = false;
    std::atomic<bool> capturing_{false};
    AudioCaptureCallback callback_;
    
    // 模拟模式下使用的计数器
    uint32_t seq_counter_ = 0;
    std::thread capture_thread_;
    
    // 模拟音频采集（用于测试环境）
    void SimulateCapture();
};

/**
 * 音频播放类
 * 使用 PortAudio 播放音频
 */
class AudioPlayback {
public:
    AudioPlayback();
    ~AudioPlayback();
    
    /**
     * 初始化音频设备
     * @param device 设备名
     * @return 是否成功
     */
    bool Initialize(const std::string& device = "default");
    
    /**
     * 开始播放
     */
    void Start();
    
    /**
     * 停止播放
     */
    void Stop();
    
    /**
     * 播放音频数据
     * @param data PCM 数据
     * @param size 数据大小
     */
    void Play(const uint8_t* data, size_t size);
    
private:
    bool initialized_ = false;
    std::atomic<bool> playing_{false};
    
    // 播放队列
    std::queue<std::vector<uint8_t>> play_queue_;
    std::mutex queue_mutex_;
};

/**
 * 语音编码器
 * 支持 PCM 直通或 Opus 压缩
 */
class VoiceEncoder {
public:
    VoiceEncoder();
    ~VoiceEncoder();
    
    /**
     * 初始化编码器
     * @return 是否成功
     */
    bool Initialize();
    
    /**
     * 编码音频帧
     * @param pcm PCM 原始数据
     * @return 编码后的数据（PCM 直通则直接返回原数据）
     */
    std::vector<uint8_t> Encode(const std::vector<uint8_t>& pcm);
    
    /**
     * 获取编码后的字节率
     */
    size_t GetBitrate() const { return kBytesPerFrame; }

private:
    bool initialized_ = false;
    // 如果使用 Opus，这里会有 OpusEncoder* 成员
};

/**
 * 语音解码器
 */
class VoiceDecoder {
public:
    VoiceDecoder();
    ~VoiceDecoder();
    
    /**
     * 初始化解码器
     * @return 是否成功
     */
    bool Initialize();
    
    /**
     * 解码音频帧
     * @param encoded 编码数据
     * @param expected_size 期望的 PCM 大小
     * @return PCM 数据
     */
    std::vector<uint8_t> Decode(const std::vector<uint8_t>& encoded, size_t expected_size = kBytesPerFrame);

private:
    bool initialized_ = false;
};

} // namespace VoiceCodec

#endif // VOICE_CODEC_H
