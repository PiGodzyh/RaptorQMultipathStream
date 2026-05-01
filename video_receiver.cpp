/**
 * video_receiver.cpp - 视频接收端实现
 */

#include "video_receiver.h"
#include "common.h"
#include "log.h"

#include <iostream>
#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <chrono>

namespace VideoTransmit {

// 辅助函数：将 AVCC 格式的 H.264 帧转换为 Annex B 格式
static std::vector<uint8_t> ConvertAvccFrameToAnnexB(const uint8_t* data, size_t size, int length_size) {
    std::vector<uint8_t> result;
    result.reserve(size + 128);
    
    size_t offset = 0;
    const uint8_t start_code[] = {0x00, 0x00, 0x00, 0x01};
    
    while (offset + length_size <= size) {
        uint32_t nal_length = 0;
        for (int i = 0; i < length_size; i++) {
            nal_length = (nal_length << 8) | data[offset + i];
        }
        offset += length_size;
        
        if (nal_length == 0 || offset + nal_length > size) {
            break;
        }
        
        result.insert(result.end(), start_code, start_code + 4);
        result.insert(result.end(), data + offset, data + offset + nal_length);
        offset += nal_length;
    }
    
    return result;
}

// 辅助函数：帧类型转字符串
static const char* FrameTypeToStr(FrameType type) {
    switch (type) {
        case FrameType::I_FRAME: return "I";
        case FrameType::P_FRAME: return "P";
        case FrameType::B_FRAME: return "B";
        case FrameType::CONFIG: return "CONFIG";
        default: return "?";
    }
}

// 帧追踪日志（调试用）
static void FrameTraceLog(const std::string& msg) {
    static std::mutex trace_mutex;
    std::lock_guard<std::mutex> lock(trace_mutex);
    std::ofstream ofs("logs/debug/frame_trace.log", std::ios::app);
    if (ofs) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        ofs << "[" << ms << "] " << msg << std::endl;
    }
}

VideoReceiver::VideoReceiver(std::shared_ptr<DataTransmit::UnifiedReceiver> unified_receiver)
    : unified_receiver_(unified_receiver)
    , video_writer_(std::make_unique<VideoCodec::VideoWriter>())
    , output_opened_(false)
    , config_received_(false)
    , running_(false)
    , next_expected_frame_seq_(0) {
}

VideoReceiver::~VideoReceiver() {
    Stop();
    CloseOutputFile();
    
    // 关闭实时显示管道
    if (pipe_fd_ >= 0) {
        close(pipe_fd_);
        pipe_fd_ = -1;
    }
    // 删除管道文件
    if (pipe_created_ && !pipe_path_.empty()) {
        unlink(pipe_path_.c_str());
        pipe_created_ = false;
    }
}

void VideoReceiver::Start() {
    if (running_) {
        return;
    }
    
    running_ = true;
    
    // 注册解码回调（使用新的多接收器支持接口）
    callback_id_ = unified_receiver_->registerDecodeCallback([this](DataPriority priority, uint32_t stream_id,
                                                                   const std::vector<uint8_t>& data) {
        if (priority == DataPriority::VIDEO) {
            OnFrameReceived(priority, stream_id, data);
        }
    });
    
    std::cout << "VideoReceiver: Started on VIDEO priority (port 9001), callback_id=" << callback_id_ << std::endl;
}

void VideoReceiver::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // 注销回调
    if (callback_id_ >= 0) {
        unified_receiver_->unregisterDecodeCallback(callback_id_);
        callback_id_ = -1;
    }
    
    std::cout << "VideoReceiver: Stopped" << std::endl;
}

bool VideoReceiver::CreateOutputFile(const std::string& filepath) {
    if (output_opened_) {
        CloseOutputFile();
    }
    
    // Bypass 模式：不创建实际 MP4 文件，只标记输出就绪
    if (bypass_mode_) {
        output_opened_ = true;
        std::cout << "VideoReceiver: Bypass mode, skip creating MP4 file" << std::endl;
        return true;
    }
    
    // 等待视频配置（静默等待，不频繁打印）
    int retry = 0;
    while (!config_received_ && retry < 300) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retry++;
    }
    
    if (!config_received_) {
        ReportError("Video config not received after 30s, cannot create output file");
        return false;
    }
    
    std::cout << "VideoReceiver: Config received, creating output file..." << std::endl;
    
    // 构建视频参数
    VideoCodec::VideoWriterParams params;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        params.width = video_config_.width;
        params.height = video_config_.height;
        params.fps_num = video_config_.fps_num;
        params.fps_den = video_config_.fps_den;
        params.bitrate = video_config_.bitrate;
        params.gop_size = video_config_.gop_size;
        params.codec_name = video_config_.codec_name;
        params.extradata = extradata_;
    }
    
    if (!video_writer_->Create(filepath, params)) {
        ReportError("Failed to create output file: " + filepath);
        return false;
    }
    
    output_opened_ = true;
    std::cout << "VideoReceiver: Created output file " << filepath << std::endl;
    return true;
}

void VideoReceiver::CloseLivePipe() {
    if (pipe_fd_ >= 0) {
        close(pipe_fd_);
        pipe_fd_ = -1;
    }
    if (pipe_created_ && !pipe_path_.empty()) {
        unlink(pipe_path_.c_str());
        pipe_created_ = false;
    }
}

void VideoReceiver::CloseOutputFile() {
    if (bypass_mode_) {
        output_opened_ = false;
        return;
    }
    std::cout << "[VideoReceiver] CloseOutputFile called, output_opened_=" << output_opened_.load() << std::endl;
    if (output_opened_) {
        std::cout << "[VideoReceiver] Closing video writer..." << std::endl;
        video_writer_->Close();
        output_opened_ = false;
    }
}

bool VideoReceiver::CreateLivePipe(const std::string& pipe_path) {
    std::cout << "[CreateLivePipe] 开始创建管道: " << pipe_path << std::endl;
    
    pipe_path_ = pipe_path;
    
    // 如果管道已存在，先删除
    unlink(pipe_path_.c_str());
    
    // 创建命名管道
    std::cout << "[CreateLivePipe] 调用 mkfifo..." << std::endl;
    if (mkfifo(pipe_path_.c_str(), 0666) < 0) {
        std::cerr << "[VideoReceiver] Failed to create pipe: " << pipe_path_ 
                  << " (error: " << strerror(errno) << ")" << std::endl;
        return false;
    }
    std::cout << "[CreateLivePipe] mkfifo 成功" << std::endl;
    
    pipe_created_ = true;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "   实时显示管道已创建" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "管道路径: " << pipe_path_ << std::endl;
    std::cout << "\n>>> 重要：请先启动 ffplay，再启动发送端 <<<" << std::endl;
    std::cout << "\n命令:" << std::endl;
    std::cout << "  ffplay -fflags nobuffer -flags low_delay -f h264 " << pipe_path_ << std::endl;
    std::cout << "或 VLC:" << std::endl;
    std::cout << "  vlc " << pipe_path_ << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // 以非阻塞方式打开管道（先返回，让调用者可以继续）
    pipe_fd_ = open(pipe_path_.c_str(), O_WRONLY | O_NONBLOCK);
    if (pipe_fd_ < 0 && errno == ENXIO) {
        // 没有读取端，这是正常的，等会再试
        std::cout << "[VideoReceiver] 等待播放器连接..." << std::endl;
    } else if (pipe_fd_ >= 0) {
        // 已经有读取端连接了
        std::cout << "[VideoReceiver] 播放器已连接，准备实时传输" << std::endl;
    } else {
        std::cerr << "[VideoReceiver] Failed to open pipe: " << strerror(errno) << std::endl;
        unlink(pipe_path_.c_str());
        pipe_created_ = false;
        return false;
    }
    
    return true;
}

bool VideoReceiver::WriteSpsPpsToPipe() {
    if (sps_pps_written_ || extradata_.empty() || pipe_fd_ < 0) {
        return false;
    }
    
    // H.264 extradata (AVCC format) 转 Annex B
    // Format: 
    //   byte 0:    configurationVersion (1)
    //   byte 1:    AVCProfileIndication
    //   byte 2:    profile_compatibility
    //   byte 3:    AVCLevelIndication
    //   byte 4:    reserved(6bits) + lengthSizeMinusOne(2bits)
    //   byte 5:    reserved(3bits) + numOfSequenceParameterSets(5bits)
    //   Then for each SPS:
    //     2 bytes: SPS length
    //     N bytes: SPS data
    //   Then:
    //     1 byte:  numOfPictureParameterSets
    //   Then for each PPS:
    //     2 bytes: PPS length
    //     N bytes: PPS data
    
    if (extradata_.size() < 7) {
        return false;
    }
    
    const uint8_t* data = extradata_.data();
    size_t size = extradata_.size();
    
    uint8_t length_size = (data[4] & 0x03) + 1;  // 通常为4
    uint8_t num_sps = data[5] & 0x1F;
    
    size_t offset = 6;
    static const uint8_t start_code[] = {0x00, 0x00, 0x00, 0x01};
    bool written_any = false;
    
    // 写入 SPS
    for (uint8_t i = 0; i < num_sps && offset + 2 <= size; i++) {
        uint16_t sps_len = (data[offset] << 8) | data[offset + 1];
        offset += 2;
        if (offset + sps_len > size) break;
        
        write(pipe_fd_, start_code, sizeof(start_code));
        write(pipe_fd_, data + offset, sps_len);
        offset += sps_len;
        written_any = true;
        std::cout << "[VideoReceiver] 写入 SPS 到管道 (" << sps_len << " bytes)" << std::endl;
    }
    
    // 写入 PPS
    if (offset < size) {
        uint8_t num_pps = data[offset++];
        for (uint8_t i = 0; i < num_pps && offset + 2 <= size; i++) {
            uint16_t pps_len = (data[offset] << 8) | data[offset + 1];
            offset += 2;
            if (offset + pps_len > size) break;
            
            write(pipe_fd_, start_code, sizeof(start_code));
            write(pipe_fd_, data + offset, pps_len);
            offset += pps_len;
            written_any = true;
            std::cout << "[VideoReceiver] 写入 PPS 到管道 (" << pps_len << " bytes)" << std::endl;
        }
    }
    
    if (written_any) {
        sps_pps_written_ = true;
    }
    return written_any;
}

bool VideoReceiver::WriteFrameToPipe(const VideoCodec::EncodedFrame& frame, uint32_t seq) {
    if (!pipe_created_) {
        return false;
    }
    
    // 如果管道还没有连接，尝试连接
    if (pipe_fd_ < 0) {
        pipe_fd_ = open(pipe_path_.c_str(), O_WRONLY | O_NONBLOCK);
        if (pipe_fd_ >= 0) {
            std::cout << "[VideoReceiver] 播放器已连接，开始实时传输" << std::endl;
        } else {
            if (seq % 30 == 0) {
                std::cerr << "[VideoReceiver] 尝试连接管道失败: " << strerror(errno) << std::endl;
            }
            return false;
        }
    }
    
    // 尝试写入
    if (pipe_fd_ >= 0) {
        // 首先写入 SPS/PPS（只在第一帧前写入一次）
        WriteSpsPpsToPipe();
        
        // 将 AVCC 格式帧转换为 Annex B 格式再写入管道
        std::vector<uint8_t> annexb_frame = ConvertAvccFrameToAnnexB(
            frame.data.data(), frame.data.size(), avcc_length_size_);
        
        ssize_t written = write(pipe_fd_, annexb_frame.data(), annexb_frame.size());
        
        if (written == static_cast<ssize_t>(annexb_frame.size())) {
            if (seq % 300 == 0) {
                LOG_MODULE_DEBUG(DataPriority::VIDEO, "Live pipe: frame " << seq 
                          << " (" << annexb_frame.size() << " bytes)");
            }
            return true;
        } else {
            std::cerr << "[VideoReceiver] 写入管道失败: written=" << written 
                      << ", expected=" << annexb_frame.size() << ", errno=" << errno << std::endl;
            if (errno == EAGAIN || errno == EPIPE) {
                close(pipe_fd_);
                pipe_fd_ = -1;
            }
            return false;
        }
    }
    
    return false;
}

void VideoReceiver::SetFrameCallback(VideoFrameCallback callback) {
    frame_callback_ = callback;
}

void VideoReceiver::SetConfigCallback(VideoConfigCallback callback) {
    config_callback_ = callback;
}

void VideoReceiver::SetErrorCallback(VideoErrorCallback callback) {
    error_callback_ = callback;
}

void VideoReceiver::SetLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_.is_open()) {
        log_file_.close();
    }
    log_file_.open(path, std::ios::app);
}

bool VideoReceiver::IsOutputOpen() const {
    return output_opened_;
}

VideoTransmitStats VideoReceiver::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

bool VideoReceiver::GetVideoConfig(VideoConfig& config) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (!config_received_) {
        return false;
    }
    config = video_config_;
    return true;
}

void VideoReceiver::OnFrameReceived(DataPriority priority, uint32_t stream_id, 
                                    const std::vector<uint8_t>& data) {
    LOG_MODULE_DEBUG(DataPriority::VIDEO, "OnDecodeComplete: stream_id=" << stream_id 
              << ", data_size=" << data.size());
    
    if (data.size() < sizeof(VideoFrameHeader)) {
        ReportError("Received data too small: " + std::to_string(data.size()));
        return;
    }
    
    // 解析头部
    VideoFrameHeader header;
    memcpy(&header, data.data(), sizeof(VideoFrameHeader));
    
    LOG_MODULE_DEBUG(DataPriority::VIDEO, "Frame type=" << static_cast<int>(header.frame_type)
              << ", seq=" << header.frame_seq);
    
    // 根据类型处理
    if (header.frame_type == FrameType::CONFIG) {
        ProcessVideoConfig(data);
    } else {
        ProcessVideoFrame(data);
    }
}

void VideoReceiver::ProcessVideoConfig(const std::vector<uint8_t>& data) {
    if (data.size() <= sizeof(VideoFrameHeader)) {
        ReportError("Invalid config data size");
        return;
    }
    
    // 提取配置数据
    size_t config_size = data.size() - sizeof(VideoFrameHeader);
    const uint8_t* config_data = data.data() + sizeof(VideoFrameHeader);
    
    // 反序列化配置
    VideoConfigPacket config_packet;
    if (!config_packet.Deserialize(config_data, config_size)) {
        ReportError("Failed to deserialize video config");
        return;
    }
    
    // 保存配置
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        video_config_ = config_packet.config;
        extradata_ = config_packet.extradata;
        if (!extradata_.empty() && extradata_.size() >= 5) {
            avcc_length_size_ = (extradata_[4] & 0x03) + 1;
            std::cout << "[VideoReceiver] AVCC length_size=" << avcc_length_size_ << std::endl;
        }
        config_received_ = true;
    }
    
    // 配置接收后，下一个期望的视频帧是seq=1
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (next_expected_frame_seq_ == 0) {
            next_expected_frame_seq_ = 1;
            std::cout << "VideoReceiver: Config received, expecting video frame seq=1" << std::endl;
        }
    }
    
    std::cout << "VideoReceiver: Received video config" << std::endl;
    std::cout << "  Resolution: " << video_config_.width << "x" << video_config_.height << std::endl;
    std::cout << "  FPS: " << video_config_.fps_num << "/" << video_config_.fps_den << std::endl;
    std::cout << "  GOP: " << video_config_.gop_size << std::endl;
    
    // 回调
    if (config_callback_) {
        config_callback_(video_config_);
    }
}

void VideoReceiver::ProcessVideoFrame(const std::vector<uint8_t>& data) {
    LOG_MODULE_DEBUG(DataPriority::VIDEO, "ProcessVideoFrame: " << data.size() << " bytes");
    
    if (data.size() <= sizeof(VideoFrameHeader)) {
        ReportError("Invalid frame data size: " + std::to_string(data.size()));
        return;
    }
    
    // 解析头部
    VideoFrameHeader header;
    memcpy(&header, data.data(), sizeof(VideoFrameHeader));
    
    // 提取H.264数据
    size_t frame_data_size = data.size() - sizeof(VideoFrameHeader);
    const uint8_t* frame_data = data.data() + sizeof(VideoFrameHeader);
    
    // 注意：从 MP4/FFmpeg 接收的 H.264 数据是 AVCC 格式（4字节长度前缀）
    // MP4 文件写入需要 AVCC 格式，管道输出需要 Annex B 格式
    // 转换在管道写入时进行，frame.data 保持原始 AVCC 格式
    
    // 构建EncodedFrame
    VideoCodec::EncodedFrame frame;
    frame.data.resize(header.frame_size);
    memcpy(frame.data.data(), frame_data, std::min(frame.data.size(), frame_data_size));
    
    // 调试：打印前几字节（VERBOSE级别）
    if (GetLogConfig().ShouldLog(LogLevel::VERBOSE, DataPriority::VIDEO)) {
        std::cout << "[VERB][Video] H.264前8字节: ";
        for (size_t i = 0; i < std::min(size_t(8), frame.data.size()); i++) {
            printf("%02x ", frame.data[i]);
        }
        std::cout << "(size=" << frame.data.size() << ")" << std::endl;
    }
    frame.pts = header.pts;
    frame.dts = header.dts;
    frame.is_key_frame = (header.frame_type == FrameType::I_FRAME);
    frame.type = static_cast<VideoCodec::FrameType>(header.frame_type);
    frame.gop_id = header.gop_id;
    
    // 保序处理
    bool should_process = false;
    uint32_t written_count = 0;
    uint32_t dropped_full_count = 0;
    uint32_t dropped_old_count = 0;
    
    // 提前保存当前帧信息（move 后仍可用）
    uint32_t current_frame_seq = header.frame_seq;
    FrameType current_frame_type = header.frame_type;
    size_t current_frame_size = frame.data.size();
    
    // 记录接收日志（网络到达时间，用于计算传输延迟）
    {
        std::lock_guard<std::mutex> log_lock(log_mutex_);
        if (log_file_.is_open()) {
            log_file_ << DataTransmit::GetCurrentTimestampUs() << "," << header.frame_seq << ","
                      << (int)header.frame_type << "," << current_frame_size << std::endl;
        }
    }
    
    std::vector<std::pair<uint32_t, VideoCodec::EncodedFrame>> frames_to_write;
    frames_to_write.reserve(1 + pending_frames_.size());
    
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        
        // Bypass 模式：直接统计，不保序、不写文件、不缓存
        if (bypass_mode_) {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.frames_received++;
            stats_.bytes_received += current_frame_size;
            switch (current_frame_type) {
                case FrameType::I_FRAME: stats_.i_frames_received++; break;
                case FrameType::P_FRAME: stats_.p_frames_received++; break;
                case FrameType::B_FRAME: stats_.b_frames_received++; break;
                default: break;
            }
            return;
        }
        
        // 如果是第一个视频帧（next_expected_frame_seq_ 为 0），直接接受它作为起始
        if (next_expected_frame_seq_ == 0) {
            next_expected_frame_seq_ = header.frame_seq;
            std::cout << "VideoReceiver: First frame received, starting from seq=" 
                      << header.frame_seq << std::endl;
        }
        else if (header.frame_seq == next_expected_frame_seq_) {
            // 期望的帧，直接处理
            should_process = true;
            next_expected_frame_seq_++;
            
            // 收集当前帧（先）
            uint32_t current_seq = header.frame_seq;
            frames_to_write.emplace_back(current_seq, std::move(frame));
            
            // 收集缓存中连续的帧（后）
            while (!pending_frames_.empty()) {
                auto it = pending_frames_.find(next_expected_frame_seq_);
                if (it == pending_frames_.end()) {
                    break;
                }
                
                frames_to_write.emplace_back(it->first, std::move(it->second));
                pending_frames_.erase(it);
                next_expected_frame_seq_++;
                written_count++;
            }
        } else if (header.frame_seq > next_expected_frame_seq_) {
            // 乱序到达，放入缓存
            LOG_MODULE_DEBUG(DataPriority::VIDEO, "Caching out-of-order frame seq=" << header.frame_seq 
                      << " type=" << FrameTypeToStr(header.frame_type)
                      << " (expecting=" << next_expected_frame_seq_ << ")");
            pending_frames_[header.frame_seq] = std::move(frame);
            
            // 缓存过大时丢弃最旧的帧（避免无限等待）
            if (pending_frames_.size() > 120) {
                // 丢弃最小的（最旧的）帧
                auto it = pending_frames_.begin();
                uint32_t dropped_seq = it->first;
                auto dropped_frame = std::move(it->second);  // 保存帧信息用于调试
                pending_frames_.erase(it);
                frame_stats_.frames_dropped_full++;
                
                // 更新期望序号为新的最小值（如果缓存不为空）
                if (!pending_frames_.empty()) {
                    next_expected_frame_seq_ = pending_frames_.begin()->first;
                    LOG_MODULE_WARN(DataPriority::VIDEO, "[帧丢弃] 缓存溢出，丢弃帧 seq=" << dropped_seq 
                              << " 类型=" << FrameTypeToStr(static_cast<FrameType>(dropped_frame.type))
                              << " 大小=" << dropped_frame.data.size()
                              << " 当前接收seq=" << header.frame_seq
                              << " 类型=" << FrameTypeToStr(header.frame_type)
                              << " 缓存=" << pending_frames_.size() + 1 << "->" << pending_frames_.size()
                              << " 跳至期望=" << next_expected_frame_seq_);
                    
                    // 重要：检查新期望的帧是否已经在缓存中
                    // 连续处理缓存中所有连续的帧
                    while (!pending_frames_.empty()) {
                        auto next_it = pending_frames_.find(next_expected_frame_seq_);
                        if (next_it == pending_frames_.end()) {
                            break; // 期望的帧还没来
                        }
                        
                        // 写入缓存帧
                        if (output_opened_) {
                            video_writer_->WriteFrame(next_it->second);
                        }
                        if (frame_callback_) {
                            frame_callback_(next_it->second);
                        }
                        
                        pending_frames_.erase(next_it);
                        next_expected_frame_seq_++;
                        written_count++;
                        frame_stats_.frames_written++;
                    }
                } else {
                    std::cout << "\n>>> [帧丢弃] 缓存溢出，丢弃帧 seq=" << dropped_seq 
                              << " 类型=" << FrameTypeToStr(static_cast<FrameType>(dropped_frame.type))
                              << " 大小=" << dropped_frame.data.size()
                              << " 当前接收seq=" << header.frame_seq
                              << " 类型=" << FrameTypeToStr(header.frame_type)
                              << "，缓存已空\n";
                }
                
                frame_stats_.frames_cached = pending_frames_.size();
                frame_stats_.Print("[接收统计] ");
            }
            
            return;
        } else {
            // 过时的帧，丢弃
            {
                std::lock_guard<std::mutex> stats_lock(frame_stats_mutex_);
                frame_stats_.frames_dropped_old++;
                frame_stats_.frames_cached = pending_frames_.size();
                LOG_MODULE_WARN(DataPriority::VIDEO, "[帧丢弃] 过时帧 seq=" << header.frame_seq 
                          << " 类型=" << FrameTypeToStr(header.frame_type)
                          << " 大小=" << current_frame_size
                          << " 期望=" << next_expected_frame_seq_
                          << " 缓存=" << pending_frames_.size()
                          << " 落后于期望=" << (next_expected_frame_seq_ - header.frame_seq) << "帧");
                frame_stats_.Print("[接收统计] ");
            }
            return;
        }
    }
    
    // 处理当前帧及缓存帧（按顺序统一写入）
    if (should_process) {
        for (auto& p : frames_to_write) {
            uint32_t seq = p.first;
            VideoCodec::EncodedFrame& f = p.second;
            
            // 写入文件
            if (output_opened_) {
                LOG_MODULE_DEBUG(DataPriority::VIDEO, "Writing frame seq=" << seq 
                          << " type=" << FrameTypeToStr(static_cast<FrameType>(f.type))
                          << " size=" << f.data.size() << " to file");
                FrameTraceLog("[RECV] seq=" + std::to_string(seq) +
                              " pts=" + std::to_string(f.pts) +
                              " dts=" + std::to_string(f.dts) +
                              " key=" + std::to_string(f.is_key_frame) +
                              " size=" + std::to_string(f.data.size()));
                bool write_ok = video_writer_->WriteFrame(f);
                if (!write_ok) {
                    std::cerr << "[VideoReceiver] Failed to write frame seq=" << seq << "\n";
                }
            }
            else system("pause");
            
            // 写入实时显示管道
            WriteFrameToPipe(f, seq);
            
            // 回调
            if (frame_callback_) {
                frame_callback_(f);
            }
        }
        
        // 更新统计
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.frames_received++;
            stats_.bytes_received += current_frame_size;
            
            switch (current_frame_type) {
                case FrameType::I_FRAME:
                    stats_.i_frames_received++;
                    break;
                case FrameType::P_FRAME:
                    stats_.p_frames_received++;
                    break;
                case FrameType::B_FRAME:
                    stats_.b_frames_received++;
                    break;
                default:
                    break;
            }
        }
        
        // 更新帧统计并打印
        {
            std::lock_guard<std::mutex> stats_lock(frame_stats_mutex_);
            // 当前帧 + 缓存中连续写入的帧
            frame_stats_.frames_written += frames_to_write.size();
            frame_stats_.frames_cached = pending_frames_.size();
            
            // 打印统计
            LOG_MODULE_DEBUG(DataPriority::VIDEO, "[帧成功] seq=" << current_frame_seq 
                      << " " << VideoCodec::FrameTypeToString(frames_to_write[0].second.type)
                      << " (" << current_frame_size << " bytes)"
                      << (written_count > 0 ? " + 缓存写入" + std::to_string(written_count) + "帧" : ""));
            frame_stats_.Print("[接收统计] ");
        }
        
        // 打印进度（每300帧，DEBUG级别）
        if (current_frame_seq % 300 == 0) {
            LOG_MODULE_DEBUG(DataPriority::VIDEO, "Received frame " << current_frame_seq 
                      << " (" << VideoCodec::FrameTypeToString(frames_to_write[0].second.type)
                      << ", " << current_frame_size << " bytes)");
        }
    }
}

void VideoReceiver::ReportError(const std::string& error) {
    std::cerr << "VideoReceiver Error: " << error << std::endl;
    if (error_callback_) {
        error_callback_(error);
    }
}

} // namespace VideoTransmit
