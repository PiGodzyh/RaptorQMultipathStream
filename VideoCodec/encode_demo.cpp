/**
 * encode_demo - 编码测试程序
 * 读取 MP4 视频文件并提取帧信息
 * 
 * 用法: ./encode_demo <input_video>
 * 示例: ./encode_demo ../data/videos/test.mp4
 */

#include "video_encoder.h"
#include <iostream>
#include <chrono>
#include <cstring>

using namespace VideoCodec;

void printUsage(const char* program) {
    std::cout << "用法: " << program << " <input_video> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数:" << std::endl;
    std::cout << "  input_video    输入视频文件路径" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -max <num>     最大读取帧数 (默认: 1000)" << std::endl;
    std::cout << "  -skip <num>    每隔多少帧跳过 (默认: 0，不跳过)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " ../data/videos/test.mp4" << std::endl;
    std::cout << "  " << program << " ../data/videos/test.mp4 -max 100" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string input_file = argv[1];
    int max_frames = 1000;
    int skip_frames = 0;

    // 解析选项
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-max" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (arg == "-skip" && i + 1 < argc) {
            skip_frames = std::atoi(argv[++i]);
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "   Video Encode Demo (帧提取测试)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "输入文件: " << input_file << std::endl;
    std::cout << "最大帧数: " << max_frames << std::endl;
    if (skip_frames > 0) {
        std::cout << "跳过间隔: " << skip_frames << std::endl;
    }
    std::cout << "========================================" << std::endl << std::endl;

    // 创建编码器（视频读取器）
    VideoEncoder encoder;
    
    // 打开视频文件
    if (!encoder.Open(input_file)) {
        std::cerr << "错误: 无法打开视频文件: " << input_file << std::endl;
        std::cerr << "错误信息: " << encoder.GetLastErrorString() << std::endl;
        return 1;
    }

    // 获取视频信息
    VideoInfo info = encoder.GetVideoInfo();
    std::cout << "视频信息:" << std::endl;
    std::cout << "  格式: " << info.format_name << std::endl;
    std::cout << "  编码: " << info.codec_name << std::endl;
    std::cout << "  分辨率: " << info.width << "x" << info.height << std::endl;
    std::cout << "  帧率: " << info.fps_num << "/" << info.fps_den << " (" 
              << (float)info.fps_num / info.fps_den << " fps)" << std::endl;
    std::cout << "  时长: " << info.duration_ms << " ms" << std::endl;
    std::cout << "  比特率: " << (info.bitrate / 1000) << " kbps" << std::endl;
    std::cout << "  总帧数: " << (info.frame_count > 0 ? std::to_string(info.frame_count) : "未知") << std::endl;
    std::cout << "  像素格式: " << info.pixel_format << std::endl;
    std::cout << std::endl;

    // 读取帧
    std::cout << "开始读取帧..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    int frame_count = 0;
    int key_frame_count = 0;
    int skip_counter = 0;
    VideoFrame frame;
    
    // 示例1: 使用 ReadFrame 逐帧读取
    while (frame_count < max_frames) {
        if (!encoder.ReadFrame(frame)) {
            break;
        }

        // 处理跳过逻辑
        if (skip_frames > 0) {
            skip_counter++;
            if (skip_counter % (skip_frames + 1) != 0) {
                continue;
            }
        }

        frame_count++;
        
        if (frame.is_key_frame) {
            key_frame_count++;
        }

        // 打印前几帧和每隔一定帧数的信息
        if (frame_count <= 5 || frame_count % 100 == 0) {
            std::cout << "  帧 " << frame_count << ": " 
                      << frame.width << "x" << frame.height
                      << " pts=" << frame.pts
                      << " " << (frame.is_key_frame ? "[I]" : "[P/B]");
            
            // 计算 YUV 数据大小
            size_t y_size = frame.linesize[0] * frame.height;
            size_t u_size = frame.linesize[1] * frame.height / 2;
            size_t v_size = frame.linesize[2] * frame.height / 2;
            std::cout << " 数据量: Y=" << y_size << " U=" << u_size << " V=" << v_size;
            std::cout << std::endl;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "读取完成统计:" << std::endl;
    std::cout << "  总帧数: " << frame_count << std::endl;
    std::cout << "  关键帧: " << key_frame_count << std::endl;
    std::cout << "  非关键帧: " << (frame_count - key_frame_count) << std::endl;
    std::cout << "  耗时: " << duration.count() << " ms" << std::endl;
    if (frame_count > 0) {
        std::cout << "  平均速度: " << (frame_count * 1000.0 / duration.count()) << " 帧/秒" << std::endl;
        std::cout << "  关键帧比例: " << (key_frame_count * 100.0 / frame_count) << "%" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    // 关闭编码器
    encoder.Close();

    // 使用回调方式重新读取（示例2）
    std::cout << std::endl;
    std::cout << "使用回调方式重新读取前10帧..." << std::endl;
    
    if (encoder.Open(input_file)) {
        int callback_count = 0;
        encoder.ReadAllFrames([&callback_count](const VideoFrame& f) -> bool {
            callback_count++;
            std::cout << "  回调帧 " << callback_count << ": "
                      << f.width << "x" << f.height << std::endl;
            return callback_count < 10;  // 继续读取返回 true
        });
        encoder.Close();
        std::cout << "回调方式读取了 " << callback_count << " 帧" << std::endl;
    }

    return 0;
}
