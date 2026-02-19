/**
 * decode_demo - 解码测试程序
 * 生成测试视频文件（MP4 格式）
 * 
 * 用法: ./decode_demo <output_video> [options]
 * 示例: ./decode_demo ../output/videos/test_output.mp4
 */

#include "video_decoder.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <cmath>

using namespace VideoCodec;

void printUsage(const char* program) {
    std::cout << "用法: " << program << " <output_video> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数:" << std::endl;
    std::cout << "  output_video   输出视频文件路径" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -w <width>     视频宽度 (默认: 640)" << std::endl;
    std::cout << "  -h <height>    视频高度 (默认: 480)" << std::endl;
    std::cout << "  -fps <fps>     帧率 (默认: 30)" << std::endl;
    std::cout << "  -n <num>       生成帧数 (默认: 300)" << std::endl;
    std::cout << "  -b <bitrate>   比特率 kbps (默认: 2000)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " ../output/videos/test.mp4" << std::endl;
    std::cout << "  " << program << " ../output/videos/test.mp4 -w 1280 -h 720 -fps 60 -n 600" << std::endl;
}

// 生成测试图案 - 彩色条
void generateColorBars(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                       int width, int height, int frame_num) {
    // 8 个彩色条
    const int bar_width = width / 8;
    
    // YUV 值（75% 彩条）
    const uint8_t bar_y[8] = {180, 168, 145, 133,  63,  51,  28,  16};
    const uint8_t bar_u[8] = {128,  44, 147,  63, 193, 109, 212, 128};
    const uint8_t bar_v[8] = {128, 136,  44,  52,  52,  60,  16, 128};
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int bar_idx = x / bar_width;
            if (bar_idx >= 8) bar_idx = 7;
            
            // 添加一些动态效果
            int frame_offset = (frame_num * 2) % 256;
            int y_val = bar_y[bar_idx] + (frame_offset - 128) / 8;
            if (y_val < 16) y_val = 16;
            if (y_val > 235) y_val = 235;
            
            y_data[y * width + x] = (uint8_t)y_val;
        }
    }
    
    // U/V 平面（宽高减半）
    int uv_width = width / 2;
    int uv_height = height / 2;
    
    for (int y = 0; y < uv_height; y++) {
        for (int x = 0; x < uv_width; x++) {
            int bar_idx = (x * 2) / bar_width;
            if (bar_idx >= 8) bar_idx = 7;
            
            u_data[y * uv_width + x] = bar_u[bar_idx];
            v_data[y * uv_width + x] = bar_v[bar_idx];
        }
    }
}

// 生成测试图案 - 移动圆球
void generateMovingBall(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                        int width, int height, int frame_num) {
    // 背景设为灰色 (Y=128, U=128, V=128)
    memset(y_data, 128, width * height);
    memset(u_data, 128, (width / 2) * (height / 2));
    memset(v_data, 128, (width / 2) * (height / 2));
    
    // 球的位置（按正弦波移动）
    float t = frame_num * 0.05f;
    int ball_x = (int)(width / 2 + (width / 3) * cos(t));
    int ball_y = (int)(height / 2 + (height / 3) * sin(t * 1.3f));
    int ball_radius = std::min(width, height) / 8;
    
    // 绘制球
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int dx = x - ball_x;
            int dy = y - ball_y;
            int dist_sq = dx * dx + dy * dy;
            
            if (dist_sq < ball_radius * ball_radius) {
                // 球内部 - 红色
                y_data[y * width + x] = 76;   // Y for red
            }
        }
    }
    
    // U/V 平面
    int uv_width = width / 2;
    int uv_height = height / 2;
    int ball_x_uv = ball_x / 2;
    int ball_y_uv = ball_y / 2;
    int ball_radius_uv = ball_radius / 2;
    
    for (int y = 0; y < uv_height; y++) {
        for (int x = 0; x < uv_width; x++) {
            int dx = x - ball_x_uv;
            int dy = y - ball_y_uv;
            int dist_sq = dx * dx + dy * dy;
            
            if (dist_sq < ball_radius_uv * ball_radius_uv) {
                // 球内部 - 红色
                u_data[y * uv_width + x] = 85;   // U for red
                v_data[y * uv_width + x] = 255;  // V for red
            }
        }
    }
}

// 生成测试图案 - 渐变
void generateGradient(uint8_t* y_data, uint8_t* u_data, uint8_t* v_data,
                      int width, int height, int frame_num) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // 水平渐变 + 时间变化
            int val = (x * 255 / width + frame_num * 2) % 256;
            y_data[y * width + x] = (uint8_t)val;
        }
    }
    
    // U/V 固定为 128（灰度）
    int uv_size = (width / 2) * (height / 2);
    memset(u_data, 128, uv_size);
    memset(v_data, 128, uv_size);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string output_file = argv[1];
    
    // 默认参数
    int width = 640;
    int height = 480;
    int fps = 30;
    int num_frames = 300;  // 10秒 @ 30fps
    int bitrate = 2000;    // kbps
    std::string pattern = "bars";  // bars, ball, gradient

    // 解析选项
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-w" && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (arg == "-h" && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (arg == "-fps" && i + 1 < argc) {
            fps = std::atoi(argv[++i]);
        } else if (arg == "-n" && i + 1 < argc) {
            num_frames = std::atoi(argv[++i]);
        } else if (arg == "-b" && i + 1 < argc) {
            bitrate = std::atoi(argv[++i]);
        } else if (arg == "-pattern" && i + 1 < argc) {
            pattern = argv[++i];
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "   Video Decode Demo (视频生成测试)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "输出文件: " << output_file << std::endl;
    std::cout << "分辨率: " << width << "x" << height << std::endl;
    std::cout << "帧率: " << fps << " fps" << std::endl;
    std::cout << "总帧数: " << num_frames << std::endl;
    std::cout << "时长: " << (num_frames / (float)fps) << " 秒" << std::endl;
    std::cout << "比特率: " << bitrate << " kbps" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    // 创建解码器（视频写入器）
    VideoDecoder decoder;
    
    // 设置编码参数
    EncodeParams params;
    params.width = width;
    params.height = height;
    params.fps_num = fps;
    params.fps_den = 1;
    params.bitrate = bitrate * 1000;  // 转换为 bps
    params.codec_name = "libx264";
    params.pixel_format = "yuv420p";

    // 创建输出文件
    if (!decoder.Create(output_file, params)) {
        std::cerr << "错误: 无法创建输出文件: " << output_file << std::endl;
        std::cerr << "错误信息: " << decoder.GetLastErrorString() << std::endl;
        return 1;
    }

    // 分配 YUV 缓冲区
    std::vector<uint8_t> y_buffer(width * height);
    std::vector<uint8_t> u_buffer((width / 2) * (height / 2));
    std::vector<uint8_t> v_buffer((width / 2) * (height / 2));

    std::cout << "开始生成视频帧..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    // 循环生成帧
    for (int i = 0; i < num_frames; i++) {
        // 生成测试图案
        if (pattern == "ball") {
            generateMovingBall(y_buffer.data(), u_buffer.data(), v_buffer.data(),
                               width, height, i);
        } else if (pattern == "gradient") {
            generateGradient(y_buffer.data(), u_buffer.data(), v_buffer.data(),
                            width, height, i);
        } else {
            // 默认彩色条
            generateColorBars(y_buffer.data(), u_buffer.data(), v_buffer.data(),
                             width, height, i);
        }

        // 写入帧
        if (!decoder.WriteYUVData(y_buffer.data(), u_buffer.data(), v_buffer.data(), i)) {
            std::cerr << "错误: 写入帧 " << i << " 失败" << std::endl;
            std::cerr << "错误信息: " << decoder.GetLastErrorString() << std::endl;
            break;
        }

        // 打印进度
        if ((i + 1) % 30 == 0 || i == num_frames - 1) {
            float progress = (i + 1) * 100.0f / num_frames;
            std::cout << "\r  进度: " << (i + 1) << "/" << num_frames 
                      << " (" << (int)progress << "%)" << std::flush;
        }
    }

    std::cout << std::endl;

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << std::endl;
    std::cout << "正在完成编码..." << std::endl;

    // 关闭解码器（刷新缓冲区并写入文件尾）
    decoder.Close();

    std::cout << "========================================" << std::endl;
    std::cout << "生成完成统计:" << std::endl;
    std::cout << "  输出文件: " << output_file << std::endl;
    std::cout << "  总帧数: " << decoder.GetFrameCount() << std::endl;
    std::cout << "  耗时: " << duration.count() << " ms" << std::endl;
    if (decoder.GetFrameCount() > 0) {
        std::cout << "  平均速度: " << (decoder.GetFrameCount() * 1000.0 / duration.count()) << " 帧/秒" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return 0;
}
