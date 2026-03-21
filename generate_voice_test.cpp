/**
 * generate_voice_test.cpp - 生成测试音频文件
 * 
 * 生成 8kHz 16bit 单声道 WAV 测试音频
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>

// WAV 文件头部
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t file_size = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;  // PCM
    uint16_t channels = 1;
    uint32_t sample_rate = 8000;
    uint32_t byte_rate = 16000;  // sample_rate * channels * 2
    uint16_t block_align = 2;    // channels * 2
    uint16_t bits_per_sample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = 0;
};

int main(int argc, char* argv[]) {
    std::string output_path = "data/voice/test_8k.wav";
    float duration_seconds = 10.0f;  // 默认10秒
    
    if (argc >= 2) {
        output_path = argv[1];
    }
    if (argc >= 3) {
        duration_seconds = std::atof(argv[2]);
    }
    
    const int sample_rate = 8000;
    const int channels = 1;
    const int bits_per_sample = 16;
    
    // 计算样本数
    int num_samples = static_cast<int>(sample_rate * duration_seconds);
    int data_size = num_samples * channels * (bits_per_sample / 8);
    
    // 创建 WAV 头部
    WavHeader header;
    header.file_size = data_size + sizeof(WavHeader) - 8;
    header.data_size = data_size;
    
    // 生成音频数据（1kHz 正弦波）
    std::vector<int16_t> samples(num_samples);
    const float frequency = 1000.0f;  // 1kHz 音调
    
    for (int i = 0; i < num_samples; i++) {
        float t = static_cast<float>(i) / sample_rate;
        samples[i] = static_cast<int16_t>(32767 * 0.5f * sinf(2.0f * 3.14159265f * frequency * t));
    }
    
    // 写入文件
    std::ofstream file(output_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to create: " << output_path << std::endl;
        return 1;
    }
    
    file.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
    file.write(reinterpret_cast<const char*>(samples.data()), data_size);
    file.close();
    
    std::cout << "Generated: " << output_path << std::endl;
    std::cout << "  Duration: " << duration_seconds << " seconds" << std::endl;
    std::cout << "  Sample rate: " << sample_rate << " Hz" << std::endl;
    std::cout << "  Channels: " << channels << std::endl;
    std::cout << "  Bits per sample: " << bits_per_sample << std::endl;
    std::cout << "  Total samples: " << num_samples << std::endl;
    std::cout << "  File size: " << (data_size + sizeof(WavHeader)) << " bytes" << std::endl;
    
    return 0;
}
