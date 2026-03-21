/**
 * voice_reader.cpp - 音频文件读取器实现
 */

#include "voice_reader.h"
#include <iostream>
#include <cstring>
#include <chrono>

namespace VoiceCodec {

// ==================== VoiceReader ====================

VoiceReader::VoiceReader() = default;

VoiceReader::~VoiceReader() {
    Close();
}

bool VoiceReader::Open(const std::string& filepath) {
    Close();
    
    file_.open(filepath, std::ios::binary);
    if (!file_.is_open()) {
        std::cerr << "[VoiceReader] Failed to open: " << filepath << std::endl;
        return false;
    }
    
    filepath_ = filepath;
    
    // 检查是否是 WAV 文件
    char header[4];
    file_.read(header, 4);
    file_.seekg(0, std::ios::beg);
    
    if (memcmp(header, "RIFF", 4) == 0) {
        // WAV 文件
        if (!ParseWavHeader()) {
            std::cerr << "[VoiceReader] Failed to parse WAV header" << std::endl;
            Close();
            return false;
        }
    } else {
        // 纯 PCM 文件
        std::cout << "[VoiceReader] Detected PCM file (no WAV header)" << std::endl;
        SetupPcmParams();
    }
    
    // 计算帧大小和总帧数
    frame_size_ = (sample_rate_ * kFrameDurationMs / 1000) * channels_ * (bits_per_sample_ / 8);
    total_frames_ = data_size_ / frame_size_;
    
    std::cout << "[VoiceReader] Opened: " << filepath << std::endl;
    std::cout << "  Sample rate: " << sample_rate_ << " Hz" << std::endl;
    std::cout << "  Channels: " << channels_ << std::endl;
    std::cout << "  Bits per sample: " << bits_per_sample_ << std::endl;
    std::cout << "  Frame size: " << frame_size_ << " bytes" << std::endl;
    std::cout << "  Total frames: " << total_frames_ << std::endl;
    std::cout << "  Duration: " << (total_frames_ * kFrameDurationMs / 1000) << " seconds" << std::endl;
    
    return true;
}

void VoiceReader::Close() {
    if (file_.is_open()) {
        file_.close();
    }
    current_frame_ = 0;
    total_frames_ = 0;
}

VoiceFrame VoiceReader::ReadFrame() {
    VoiceFrame frame;
    
    if (!file_.is_open() || current_frame_ >= total_frames_) {
        frame.is_valid = false;
        return frame;
    }
    
    // 定位到数据位置
    file_.seekg(data_offset_ + current_frame_ * frame_size_, std::ios::beg);
    
    // 读取帧数据
    frame.pcm_data.resize(frame_size_);
    file_.read(reinterpret_cast<char*>(frame.pcm_data.data()), frame_size_);
    
    if (file_.gcount() < static_cast<std::streamsize>(frame_size_)) {
        // 最后一帧可能不完整，填充静音
        size_t bytes_read = file_.gcount();
        frame.pcm_data.resize(bytes_read);
        // 填充剩余部分为0（静音）
        frame.pcm_data.resize(frame_size_, 0);
    }
    
    frame.seq = current_frame_;
    frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.is_valid = true;
    
    current_frame_++;
    
    return frame;
}

bool VoiceReader::ParseWavHeader() {
    WavHeader header;
    file_.read(reinterpret_cast<char*>(&header), sizeof(WavHeader));
    
    if (file_.gcount() != sizeof(WavHeader)) {
        return false;
    }
    
    // 验证 RIFF 标记
    if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0) {
        return false;
    }
    
    // 验证 fmt 标记
    if (memcmp(header.fmt, "fmt ", 4) != 0) {
        return false;
    }
    
    // 提取参数
    sample_rate_ = header.sample_rate;
    channels_ = header.channels;
    bits_per_sample_ = header.bits_per_sample;
    
    // 查找 data chunk（可能不在固定位置）
    if (memcmp(header.data, "data", 4) == 0) {
        data_size_ = header.data_size;
        data_offset_ = sizeof(WavHeader);
    } else {
        // 需要查找 data chunk
        char chunk_id[4];
        uint32_t chunk_size;
        
        // 跳过 fmt chunk 剩余部分
        uint32_t fmt_remaining = header.fmt_size - 16;
        file_.seekg(fmt_remaining, std::ios::cur);
        
        // 查找 data chunk
        while (file_.read(chunk_id, 4)) {
            file_.read(reinterpret_cast<char*>(&chunk_size), 4);
            
            if (memcmp(chunk_id, "data", 4) == 0) {
                data_size_ = chunk_size;
                data_offset_ = file_.tellg();
                break;
            } else {
                // 跳过这个 chunk
                file_.seekg(chunk_size, std::ios::cur);
            }
        }
    }
    
    if (data_size_ == 0) {
        return false;
    }
    
    // 验证参数
    if (header.audio_format != 1) {  // 1 = PCM
        std::cerr << "[VoiceReader] Warning: Not PCM format (" << header.audio_format << ")" << std::endl;
    }
    
    return true;
}

void VoiceReader::SetupPcmParams() {
    // 默认参数
    sample_rate_ = 8000;
    channels_ = 1;
    bits_per_sample_ = 16;
    
    // 获取文件大小
    file_.seekg(0, std::ios::end);
    data_size_ = file_.tellg();
    file_.seekg(0, std::ios::beg);
    
    data_offset_ = 0;
}

// ==================== VoiceWriter ====================

VoiceWriter::VoiceWriter() = default;

VoiceWriter::~VoiceWriter() {
    Close();
}

bool VoiceWriter::Create(const std::string& filepath, 
                         uint32_t sample_rate,
                         uint16_t channels,
                         uint16_t bits_per_sample) {
    Close();
    
    file_.open(filepath, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) {
        std::cerr << "[VoiceWriter] Failed to create: " << filepath << std::endl;
        return false;
    }
    
    filepath_ = filepath;
    sample_rate_ = sample_rate;
    channels_ = channels;
    bits_per_sample_ = bits_per_sample;
    frame_size_ = (sample_rate * kFrameDurationMs / 1000) * channels * (bits_per_sample / 8);
    
    written_frames_ = 0;
    written_bytes_ = 0;
    
    // 写入临时 WAV 头部（稍后更新）
    WriteWavHeader();
    
    std::cout << "[VoiceWriter] Created: " << filepath << std::endl;
    std::cout << "  Sample rate: " << sample_rate_ << " Hz" << std::endl;
    std::cout << "  Channels: " << channels_ << std::endl;
    std::cout << "  Bits per sample: " << bits_per_sample_ << std::endl;
    
    return true;
}

void VoiceWriter::Close() {
    if (file_.is_open()) {
        // 更新 WAV 头部
        UpdateWavHeader();
        file_.close();
        
        std::cout << "[VoiceWriter] Closed: " << filepath_ << std::endl;
        std::cout << "  Written frames: " << written_frames_ << std::endl;
        std::cout << "  Written bytes: " << written_bytes_ << std::endl;
        std::cout << "  Duration: " << (written_frames_ * kFrameDurationMs / 1000) << " seconds" << std::endl;
    }
}

bool VoiceWriter::WriteFrame(const VoiceFrame& frame) {
    if (!file_.is_open()) {
        return false;
    }
    
    return WritePcm(frame.pcm_data);
}

bool VoiceWriter::WritePcm(const std::vector<uint8_t>& pcm_data) {
    if (!file_.is_open()) {
        return false;
    }
    
    file_.write(reinterpret_cast<const char*>(pcm_data.data()), pcm_data.size());
    
    if (file_.good()) {
        written_bytes_ += pcm_data.size();
        written_frames_++;
        return true;
    }
    
    return false;
}

void VoiceWriter::WriteWavHeader() {
    WavHeader header;
    
    memcpy(header.riff, "RIFF", 4);
    header.file_size = 0;  // 稍后更新
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1;  // PCM
    header.channels = channels_;
    header.sample_rate = sample_rate_;
    header.byte_rate = sample_rate_ * channels_ * (bits_per_sample_ / 8);
    header.block_align = channels_ * (bits_per_sample_ / 8);
    header.bits_per_sample = bits_per_sample_;
    memcpy(header.data, "data", 4);
    header.data_size = 0;  // 稍后更新
    
    header_position_ = file_.tellp();
    file_.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
}

void VoiceWriter::UpdateWavHeader() {
    if (!file_.is_open() || header_position_ == 0) {
        return;
    }
    
    // 回到头部位置
    file_.seekp(header_position_, std::ios::beg);
    
    // 更新文件大小
    uint32_t file_size = written_bytes_ + sizeof(WavHeader) - 8;
    file_.seekp(4, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(&file_size), 4);
    
    // 更新数据大小
    uint32_t data_size = written_bytes_;
    file_.seekp(40, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(&data_size), 4);
}

} // namespace VoiceCodec
