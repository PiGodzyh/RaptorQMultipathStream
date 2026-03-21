/**
 * voice_codec.cpp - 语音编解码实现
 * 
 * 模拟模式：生成/消费测试音频数据
 * 实际部署时替换为 PortAudio 实现
 */

#include "voice_codec.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <algorithm>

namespace VoiceCodec {

// ==================== AudioCapture ====================

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    Stop();
}

bool AudioCapture::Initialize(const std::string& device) {
    std::cout << "[AudioCapture] Initializing device: " << device << std::endl;
    
    // 检查 PortAudio 是否可用
    #ifdef HAS_PORTAUDIO
    // 实际 PortAudio 初始化代码...
    std::cout << "[AudioCapture] Using PortAudio" << std::endl;
    #else
    std::cout << "[AudioCapture] PortAudio not available, using simulation mode" << std::endl;
    #endif
    
    initialized_ = true;
    return true;
}

void AudioCapture::Start(AudioCaptureCallback callback) {
    if (!initialized_ || capturing_) return;
    
    callback_ = callback;
    capturing_ = true;
    
    std::cout << "[AudioCapture] Started (" << kFrameDurationMs << "ms frames, " 
              << kBytesPerFrame << " bytes/frame)" << std::endl;
    
    #ifdef HAS_PORTAUDIO
    // 实际 PortAudio 启动代码...
    #else
    // 模拟模式：后台线程生成音频数据
    capture_thread_ = std::thread([this]() { SimulateCapture(); });
    #endif
}

void AudioCapture::Stop() {
    if (!capturing_) return;
    
    capturing_ = false;
    
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    
    std::cout << "[AudioCapture] Stopped" << std::endl;
}

std::vector<uint8_t> AudioCapture::ReadFrame() {
    std::vector<uint8_t> frame(kBytesPerFrame);
    
    // 生成简单的测试音频（正弦波）
    static const float frequency = 440.0f;  // A4 音
    static uint64_t sample_index = 0;
    
    int16_t* samples = reinterpret_cast<int16_t*>(frame.data());
    size_t sample_count = kBytesPerFrame / sizeof(int16_t);
    
    for (size_t i = 0; i < sample_count; i++) {
        float t = static_cast<float>(sample_index + i) / kSampleRate;
        samples[i] = static_cast<int16_t>(32767 * 0.5f * sinf(2.0f * 3.14159265f * frequency * t));
    }
    sample_index += sample_count;
    
    return frame;
}

void AudioCapture::SimulateCapture() {
    auto next_frame_time = std::chrono::steady_clock::now();
    const auto frame_interval = std::chrono::milliseconds(kFrameDurationMs);
    
    while (capturing_) {
        // 采集一帧
        auto pcm_data = ReadFrame();
        
        AudioFrame frame;
        frame.seq = seq_counter_++;
        frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        frame.data = std::move(pcm_data);
        
        // 回调通知
        if (callback_) {
            callback_(frame);
        }
        
        // 等待下一帧时间
        next_frame_time += frame_interval;
        std::this_thread::sleep_until(next_frame_time);
    }
}

// ==================== AudioPlayback ====================

AudioPlayback::AudioPlayback() = default;

AudioPlayback::~AudioPlayback() {
    Stop();
}

bool AudioPlayback::Initialize(const std::string& device) {
    std::cout << "[AudioPlayback] Initializing device: " << device << std::endl;
    
    #ifdef HAS_PORTAUDIO
    std::cout << "[AudioPlayback] Using PortAudio" << std::endl;
    #else
    std::cout << "[AudioPlayback] PortAudio not available, using simulation mode" << std::endl;
    #endif
    
    initialized_ = true;
    return true;
}

void AudioPlayback::Start() {
    if (!initialized_ || playing_) return;
    
    playing_ = true;
    std::cout << "[AudioPlayback] Started" << std::endl;
}

void AudioPlayback::Stop() {
    if (!playing_) return;
    
    playing_ = false;
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!play_queue_.empty()) play_queue_.pop();
    
    std::cout << "[AudioPlayback] Stopped" << std::endl;
}

void AudioPlayback::Play(const uint8_t* data, size_t size) {
    if (!playing_) return;
    
    #ifdef HAS_PORTAUDIO
    // 实际播放代码...
    #else
    // 模拟模式：只是简单地丢弃数据
    // 可以在这里添加音频级别检测等调试信息
    static uint64_t total_played = 0;
    total_played += size;
    
    // 每100帧打印一次（约2秒）
    if (total_played % (kBytesPerFrame * 100) == 0) {
        std::cout << "[AudioPlayback] Played " << (total_played / 1024) << " KB" << std::endl;
    }
    #endif
}

// ==================== VoiceEncoder ====================

VoiceEncoder::VoiceEncoder() = default;

VoiceEncoder::~VoiceEncoder() = default;

bool VoiceEncoder::Initialize() {
    std::cout << "[VoiceEncoder] Initialized (PCM pass-through mode)" << std::endl;
    initialized_ = true;
    return true;
}

std::vector<uint8_t> VoiceEncoder::Encode(const std::vector<uint8_t>& pcm) {
    // PCM 直通模式：直接返回原数据
    // 如果使用 Opus，这里会进行压缩
    return pcm;
}

// ==================== VoiceDecoder ====================

VoiceDecoder::VoiceDecoder() = default;

VoiceDecoder::~VoiceDecoder() = default;

bool VoiceDecoder::Initialize() {
    std::cout << "[VoiceDecoder] Initialized (PCM pass-through mode)" << std::endl;
    initialized_ = true;
    return true;
}

std::vector<uint8_t> VoiceDecoder::Decode(const std::vector<uint8_t>& encoded, size_t expected_size) {
    // PCM 直通模式
    if (encoded.size() == expected_size) {
        return encoded;
    }
    
    // 如果大小不匹配，可能是压缩数据，需要解压
    // 这里简单填充静音
    std::vector<uint8_t> result(expected_size);
    memcpy(result.data(), encoded.data(), std::min(encoded.size(), expected_size));
    return result;
}

} // namespace VoiceCodec
