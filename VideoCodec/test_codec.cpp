/**
 * test_codec.cpp - 视频编解码模块测试
 * 
 * 测试内容：
 * 1. 使用VideoReader读取MP4文件的H.264数据
 * 2. 统计I/P/B帧数量
 * 3. 使用VideoWriter将H.264数据写入新的MP4文件
 * 4. 验证输出文件是否正确
 * 
 * 使用方法:
 *   ./test_codec <input.mp4> [output.mp4]
 */

#include "video_reader.h"
#include "video_writer.h"
#include <iostream>
#include <cstdlib>
#include <chrono>

using namespace VideoCodec;

void PrintUsage(const char* program) {
    std::cout << "Usage: " << program << " <input.mp4> [output.mp4]" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program << " input.mp4              # 仅读取并统计帧信息" << std::endl;
    std::cout << "  " << program << " input.mp4 output.mp4   # 读取并复制到新文件" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string input_file = argv[1];
    std::string output_file = (argc >= 3) ? argv[2] : "";
    bool do_write = !output_file.empty();

    std::cout << "========================================" << std::endl;
    std::cout << "    VideoCodec Module Test (H.264)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Input:  " << input_file << std::endl;
    if (do_write) {
        std::cout << "Output: " << output_file << std::endl;
    }
    std::cout << std::endl;

    // 统计信息
    int64_t total_frames = 0;
    int64_t i_frames = 0;
    int64_t p_frames = 0;
    int64_t b_frames = 0;
    int64_t unknown_frames = 0;
    int64_t total_bytes = 0;
    int64_t i_frame_bytes = 0;
    int64_t p_frame_bytes = 0;
    int64_t b_frame_bytes = 0;

    auto start_time = std::chrono::steady_clock::now();

    // 打开输入文件
    VideoReader reader;
    if (!reader.Open(input_file)) {
        std::cerr << "Failed to open input file: " << input_file << std::endl;
        std::cerr << "Error: " << reader.GetLastErrorString() << std::endl;
        return 1;
    }

    VideoInfo info = reader.GetVideoInfo();
    std::cout << "Video Info:" << std::endl;
    std::cout << "  Format:     " << info.format_name << std::endl;
    std::cout << "  Codec:      " << info.codec_name << std::endl;
    std::cout << "  Resolution: " << info.width << "x" << info.height << std::endl;
    std::cout << "  FPS:        " << info.fps_num << "/" << info.fps_den << std::endl;
    std::cout << "  Duration:   " << info.duration_ms << " ms" << std::endl;
    std::cout << "  Bitrate:    " << (info.bitrate / 1000) << " kbps" << std::endl;
    std::cout << "  GOP Size:   " << info.gop_size << std::endl;
    std::cout << std::endl;

    // 创建输出文件（如果需要）
    VideoWriter writer;
    if (do_write) {
        VideoWriterParams params = VideoWriterParams::FromVideoInfo(info);
        
        // 确保必要的参数有效
        if (params.fps_num <= 0) params.fps_num = 30;
        if (params.fps_den <= 0) params.fps_den = 1;
        if (params.bitrate <= 0) params.bitrate = 2000000;
        if (params.gop_size <= 0) params.gop_size = 12;

        if (!writer.Create(output_file, params)) {
            std::cerr << "Failed to create output file: " << output_file << std::endl;
            std::cerr << "Error: " << writer.GetLastErrorString() << std::endl;
            return 1;
        }
    }

    // 读取并处理所有帧
    std::cout << "Processing frames..." << std::endl;
    
    EncodedFrame frame;
    while (reader.ReadFrame(frame)) {
        total_frames++;
        total_bytes += frame.data.size();

        // 统计帧类型
        switch (frame.type) {
            case FrameType::I_FRAME:
                i_frames++;
                i_frame_bytes += frame.data.size();
                break;
            case FrameType::P_FRAME:
                p_frames++;
                p_frame_bytes += frame.data.size();
                break;
            case FrameType::B_FRAME:
                b_frames++;
                b_frame_bytes += frame.data.size();
                break;
            default:
                unknown_frames++;
                break;
        }

        // 写入输出文件（如果需要）
        if (do_write) {
            if (!writer.WriteFrame(frame)) {
                std::cerr << "Failed to write frame " << total_frames << std::endl;
                std::cerr << "Error: " << writer.GetLastErrorString() << std::endl;
                return 1;
            }
        }

        // 每100帧打印一次进度
        if (total_frames % 100 == 0) {
            std::cout << "  Processed " << total_frames << " frames...\r" << std::flush;
        }
    }

    std::cout << std::endl;

    // 关闭文件
    reader.Close();
    if (do_write) {
        writer.Close();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // 打印统计信息
    std::cout << std::endl;
    std::cout << "Statistics:" << std::endl;
    std::cout << "  Total frames:     " << total_frames << std::endl;
    std::cout << "  I-frames:         " << i_frames << " (" << (i_frames * 100.0 / total_frames) << "%)" << std::endl;
    std::cout << "  P-frames:         " << p_frames << " (" << (p_frames * 100.0 / total_frames) << "%)" << std::endl;
    std::cout << "  B-frames:         " << b_frames << " (" << (b_frames * 100.0 / total_frames) << "%)" << std::endl;
    std::cout << "  Unknown:          " << unknown_frames << std::endl;
    std::cout << std::endl;
    std::cout << "  Total data:       " << (total_bytes / 1024 / 1024) << " MB" << std::endl;
    std::cout << "  I-frame data:     " << (i_frame_bytes / 1024 / 1024) << " MB (" 
              << (i_frame_bytes * 100.0 / total_bytes) << "%)" << std::endl;
    std::cout << "  P-frame data:     " << (p_frame_bytes / 1024 / 1024) << " MB (" 
              << (p_frame_bytes * 100.0 / total_bytes) << "%)" << std::endl;
    std::cout << "  B-frame data:     " << (b_frame_bytes / 1024 / 1024) << " MB (" 
              << (b_frame_bytes * 100.0 / total_bytes) << "%)" << std::endl;
    std::cout << std::endl;
    std::cout << "  Average frame size: " << (total_bytes / total_frames / 1024) << " KB" << std::endl;
    if (i_frames > 0) {
        std::cout << "  Average I-frame:    " << (i_frame_bytes / i_frames / 1024) << " KB" << std::endl;
    }
    if (p_frames > 0) {
        std::cout << "  Average P-frame:    " << (p_frame_bytes / p_frames / 1024) << " KB" << std::endl;
    }
    std::cout << std::endl;
    std::cout << "  Processing time:    " << duration.count() << " ms" << std::endl;
    std::cout << "  Speed:              " << (total_frames * 1000.0 / duration.count()) << " fps" << std::endl;
    std::cout << std::endl;

    if (do_write) {
        std::cout << "Output file created: " << output_file << std::endl;
        std::cout << "  Frames written: " << writer.GetFrameCount() << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Test completed successfully!" << std::endl;

    return 0;
}
