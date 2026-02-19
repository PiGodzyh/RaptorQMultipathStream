/**
 * transcode_demo - 视频转码测试程序
 * 从输入视频读取帧，然后直接编码输出到目标文件
 * 
 * 用法: ./transcode_demo <input_video> <output_video> [options]
 * 示例: 
 *   ./transcode_demo ../data/videos/test.mp4 ../output/videos/output.mp4
 *   ./transcode_demo ../data/videos/test.mp4 ../output/videos/output.mp4 -w 640 -h 480 -fps 30
 */

#include "video_encoder.h"
#include "video_decoder.h"
#include <iostream>
#include <chrono>
#include <cstring>

using namespace VideoCodec;

void printUsage(const char* program) {
    std::cout << "用法: " << program << " <input_video> <output_video> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数:" << std::endl;
    std::cout << "  input_video    输入视频文件路径" << std::endl;
    std::cout << "  output_video   输出视频文件路径" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -w <width>     输出视频宽度 (默认: 与输入相同)" << std::endl;
    std::cout << "  -h <height>    输出视频高度 (默认: 与输入相同)" << std::endl;
    std::cout << "  -fps <fps>     输出帧率 (默认: 与输入相同)" << std::endl;
    std::cout << "  -b <bitrate>   输出比特率 kbps (默认: 2000)" << std::endl;
    std::cout << "  -max <num>     最大处理帧数 (默认: 全部)" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program << " ../data/videos/test.mp4 ../output/videos/output.mp4" << std::endl;
    std::cout << "  " << program << " ../data/videos/test.mp4 ../output/videos/output.mp4 -w 640 -h 480" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string input_file = argv[1];
    std::string output_file = argv[2];
    
    // 默认参数（-1 表示使用输入视频的参数）
    int out_width = -1;
    int out_height = -1;
    int out_fps = -1;
    int bitrate = 2000;  // kbps
    int max_frames = -1;  // -1 表示全部

    // 解析选项
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-w" && i + 1 < argc) {
            out_width = std::atoi(argv[++i]);
        } else if (arg == "-h" && i + 1 < argc) {
            out_height = std::atoi(argv[++i]);
        } else if (arg == "-fps" && i + 1 < argc) {
            out_fps = std::atoi(argv[++i]);
        } else if (arg == "-b" && i + 1 < argc) {
            bitrate = std::atoi(argv[++i]);
        } else if (arg == "-max" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "   Video Transcode Demo (转码测试)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "输入文件: " << input_file << std::endl;
    std::cout << "输出文件: " << output_file << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    // ========== 第一步：打开输入视频 ==========
    std::cout << "[1/3] 正在打开输入视频..." << std::endl;
    
    VideoEncoder encoder;
    if (!encoder.Open(input_file)) {
        std::cerr << "错误: 无法打开输入视频: " << input_file << std::endl;
        std::cerr << "错误信息: " << encoder.GetLastErrorString() << std::endl;
        return 1;
    }

    // 获取输入视频信息
    VideoInfo in_info = encoder.GetVideoInfo();
    std::cout << "输入视频信息:" << std::endl;
    std::cout << "  格式: " << in_info.format_name << std::endl;
    std::cout << "  编码: " << in_info.codec_name << std::endl;
    std::cout << "  分辨率: " << in_info.width << "x" << in_info.height << std::endl;
    std::cout << "  帧率: " << in_info.fps_num << "/" << in_info.fps_den 
              << " (" << (float)in_info.fps_num / in_info.fps_den << " fps)" << std::endl;
    std::cout << "  时长: " << in_info.duration_ms << " ms" << std::endl;
    std::cout << "  总帧数: " << (in_info.frame_count > 0 ? std::to_string(in_info.frame_count) : "未知") << std::endl;
    std::cout << std::endl;

    // 确定输出参数
    if (out_width <= 0) out_width = in_info.width;
    if (out_height <= 0) out_height = in_info.height;
    int fps_num = (out_fps > 0) ? out_fps : in_info.fps_num;
    int fps_den = (out_fps > 0) ? 1 : in_info.fps_den;
    
    if (max_frames < 0) {
        max_frames = (in_info.frame_count > 0) ? in_info.frame_count : 10000;
    }

    // ========== 第二步：创建输出视频 ==========
    std::cout << "[2/3] 正在创建输出视频..." << std::endl;
    
    VideoDecoder decoder;
    EncodeParams params;
    params.width = out_width;
    params.height = out_height;
    params.fps_num = fps_num;
    params.fps_den = fps_den;
    params.bitrate = bitrate * 1000;
    params.codec_name = "libx264";
    params.pixel_format = "yuv420p";

    if (!decoder.Create(output_file, params)) {
        std::cerr << "错误: 无法创建输出视频: " << output_file << std::endl;
        std::cerr << "错误信息: " << decoder.GetLastErrorString() << std::endl;
        encoder.Close();
        return 1;
    }

    std::cout << "输出视频参数:" << std::endl;
    std::cout << "  分辨率: " << out_width << "x" << out_height << std::endl;
    std::cout << "  帧率: " << fps_num << "/" << fps_den << std::endl;
    std::cout << "  比特率: " << bitrate << " kbps" << std::endl;
    std::cout << "  最大帧数: " << max_frames << std::endl;
    std::cout << std::endl;

    // ========== 第三步：转码（读取 -> 写入） ==========
    std::cout << "[3/3] 开始转码..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    
    int frame_count = 0;
    int key_frame_count = 0;
    VideoFrame frame;
    
    // 如果分辨率变化，需要分配转换缓冲区
    bool need_scale = (out_width != in_info.width || out_height != in_info.height);
    std::vector<uint8_t> y_buffer, u_buffer, v_buffer;
    
    if (need_scale) {
        std::cout << "注意: 分辨率从 " << in_info.width << "x" << in_info.height 
                  << " 调整为 " << out_width << "x" << out_height << std::endl;
        // 这里简化处理：如果分辨率不同，暂时只做裁剪/填充
        // 实际应该用 sws_scale 进行缩放
        std::cout << "警告: 分辨率调整需要实现缩放逻辑，这里使用原始分辨率" << std::endl;
        out_width = in_info.width;
        out_height = in_info.height;
    }

    while (frame_count < max_frames) {
        // 读取一帧
        if (!encoder.ReadFrame(frame)) {
            break;
        }

        frame_count++;
        if (frame.is_key_frame) {
            key_frame_count++;
        }

        // 准备 YUV 数据
        // frame.data[0] 是 Y 平面，frame.linesize[0] 是行字节数
        // frame.data[1] 是 U 平面，frame.data[2] 是 V 平面
        
        int y_size = frame.width * frame.height;
        int uv_width = frame.width / 2;
        int uv_height = frame.height / 2;
        int uv_size = uv_width * uv_height;

        // 将数据复制到连续缓冲区（因为 linesize 可能包含 padding）
        if (y_buffer.size() < (size_t)y_size) {
            y_buffer.resize(y_size);
            u_buffer.resize(uv_size);
            v_buffer.resize(uv_size);
        }

        // 复制 Y 平面
        for (int y = 0; y < frame.height; y++) {
            memcpy(y_buffer.data() + y * frame.width,
                   frame.data[0] + y * frame.linesize[0],
                   frame.width);
        }

        // 复制 U 平面
        for (int y = 0; y < uv_height; y++) {
            memcpy(u_buffer.data() + y * uv_width,
                   frame.data[1] + y * frame.linesize[1],
                   uv_width);
        }

        // 复制 V 平面
        for (int y = 0; y < uv_height; y++) {
            memcpy(v_buffer.data() + y * uv_width,
                   frame.data[2] + y * frame.linesize[2],
                   uv_width);
        }

        // 写入帧
        if (!decoder.WriteYUVData(y_buffer.data(), u_buffer.data(), v_buffer.data(), frame_count - 1)) {
            std::cerr << "错误: 写入帧 " << frame_count << " 失败" << std::endl;
            break;
        }

        // 打印进度
        if (frame_count % 30 == 0 || frame_count == 1) {
            float progress = max_frames > 0 ? (frame_count * 100.0f / max_frames) : 0;
            std::cout << "\r  进度: " << frame_count;
            if (max_frames > 0) {
                std::cout << "/" << max_frames << " (" << (int)progress << "%)";
            }
            std::cout << std::flush;
        }
    }

    std::cout << std::endl << std::endl;

    auto encode_end_time = std::chrono::steady_clock::now();
    auto encode_duration = std::chrono::duration_cast<std::chrono::milliseconds>(encode_end_time - start_time);

    // ========== 第四步：完成 ==========
    std::cout << "正在完成编码..." << std::endl;
    
    // 关闭资源（先关闭解码器完成编码，再关闭编码器）
    decoder.Close();
    encoder.Close();

    auto end_time = std::chrono::steady_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "转码完成统计:" << std::endl;
    std::cout << "  输入文件: " << input_file << std::endl;
    std::cout << "  输出文件: " << output_file << std::endl;
    std::cout << "  处理帧数: " << frame_count << std::endl;
    std::cout << "  关键帧数: " << key_frame_count << std::endl;
    std::cout << "  转码耗时: " << encode_duration.count() << " ms" << std::endl;
    std::cout << "  总耗时: " << total_duration.count() << " ms" << std::endl;
    if (frame_count > 0) {
        std::cout << "  平均速度: " << (frame_count * 1000.0 / encode_duration.count()) << " 帧/秒" << std::endl;
        float in_fps = (float)in_info.fps_num / in_info.fps_den;
        float process_fps = frame_count * 1000.0 / encode_duration.count();
        std::cout << "  处理倍数: " << (process_fps / in_fps) << "x (相对于实时)" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return 0;
}
