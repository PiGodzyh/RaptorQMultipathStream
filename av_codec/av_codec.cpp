/**
 * @file av_codec.cpp
 * @brief 音视频编解码模块实现
 */

#include "av_codec.h"
#include <cstring>
#include <iostream>
#include <algorithm>

namespace AVCodecModule {

// ============================================================================
// MediaPacket 实现
// ============================================================================

std::vector<uint8_t> MediaPacket::serialize() const {
    // 格式: [type(1)] [index(4)] [pts(8)] [dts(8)] [is_key(1)] [data_size(4)] [data...]
    std::vector<uint8_t> result;
    size_t header_size = 1 + 4 + 8 + 8 + 1 + 4;
    result.reserve(header_size + data.size());
    
    // type
    result.push_back(static_cast<uint8_t>(type));
    
    // index (little endian)
    result.push_back(index & 0xFF);
    result.push_back((index >> 8) & 0xFF);
    result.push_back((index >> 16) & 0xFF);
    result.push_back((index >> 24) & 0xFF);
    
    // pts
    for (int i = 0; i < 8; i++) {
        result.push_back((pts >> (i * 8)) & 0xFF);
    }
    
    // dts
    for (int i = 0; i < 8; i++) {
        result.push_back((dts >> (i * 8)) & 0xFF);
    }
    
    // is_key_frame
    result.push_back(is_key_frame ? 1 : 0);
    
    // data_size
    uint32_t data_size = static_cast<uint32_t>(data.size());
    result.push_back(data_size & 0xFF);
    result.push_back((data_size >> 8) & 0xFF);
    result.push_back((data_size >> 16) & 0xFF);
    result.push_back((data_size >> 24) & 0xFF);
    
    // data
    result.insert(result.end(), data.begin(), data.end());
    
    return result;
}

bool MediaPacket::deserialize(const uint8_t* data, size_t size, MediaPacket& packet) {
    size_t header_size = 1 + 4 + 8 + 8 + 1 + 4;
    if (size < header_size) {
        return false;
    }
    
    size_t offset = 0;
    
    // type
    packet.type = static_cast<FrameType>(data[offset++]);
    
    // index
    packet.index = data[offset] | (data[offset+1] << 8) | 
                   (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    
    // pts
    packet.pts = 0;
    for (int i = 0; i < 8; i++) {
        packet.pts |= static_cast<int64_t>(data[offset + i]) << (i * 8);
    }
    offset += 8;
    
    // dts
    packet.dts = 0;
    for (int i = 0; i < 8; i++) {
        packet.dts |= static_cast<int64_t>(data[offset + i]) << (i * 8);
    }
    offset += 8;
    
    // is_key_frame
    packet.is_key_frame = (data[offset++] != 0);
    
    // data_size
    uint32_t data_size = data[offset] | (data[offset+1] << 8) | 
                         (data[offset+2] << 16) | (data[offset+3] << 24);
    offset += 4;
    
    if (size < offset + data_size) {
        return false;
    }
    
    // data
    packet.data.assign(data + offset, data + offset + data_size);
    
    return true;
}

bool MediaPacket::deserialize(const std::vector<uint8_t>& data, MediaPacket& packet) {
    return deserialize(data.data(), data.size(), packet);
}

// ============================================================================
// MediaHeader 实现
// ============================================================================

std::vector<uint8_t> MediaHeader::serialize() const {
    std::vector<uint8_t> result;
    
    auto writeU32 = [&result](uint32_t val) {
        result.push_back(val & 0xFF);
        result.push_back((val >> 8) & 0xFF);
        result.push_back((val >> 16) & 0xFF);
        result.push_back((val >> 24) & 0xFF);
    };
    
    auto writeI32 = [&result](int32_t val) {
        result.push_back(val & 0xFF);
        result.push_back((val >> 8) & 0xFF);
        result.push_back((val >> 16) & 0xFF);
        result.push_back((val >> 24) & 0xFF);
    };
    
    auto writeI64 = [&result](int64_t val) {
        for (int i = 0; i < 8; i++) {
            result.push_back((val >> (i * 8)) & 0xFF);
        }
    };
    
    // Magic
    writeU32(MAGIC);
    
    // Video info
    result.push_back(info.has_video ? 1 : 0);
    writeI32(info.video_width);
    writeI32(info.video_height);
    writeI32(static_cast<int32_t>(info.video_pixel_format));
    writeI32(info.video_time_base.num);
    writeI32(info.video_time_base.den);
    writeI32(info.video_stream_index);
    writeI32(static_cast<int32_t>(info.video_codec_id));
    writeU32(info.video_codec_tag);
    
    // Audio info
    result.push_back(info.has_audio ? 1 : 0);
    writeI32(info.audio_sample_rate);
    writeI32(info.audio_channels);
    writeI32(static_cast<int32_t>(info.audio_sample_format));
    writeI32(info.audio_time_base.num);
    writeI32(info.audio_time_base.den);
    writeI32(info.audio_stream_index);
    writeI32(static_cast<int32_t>(info.audio_codec_id));
    writeU32(info.audio_codec_tag);
    
    // Duration and bitrate
    writeI64(info.duration);
    writeI64(info.bit_rate);
    
    // Video extradata
    writeU32(static_cast<uint32_t>(video_extradata.size()));
    result.insert(result.end(), video_extradata.begin(), video_extradata.end());
    
    // Audio extradata
    writeU32(static_cast<uint32_t>(audio_extradata.size()));
    result.insert(result.end(), audio_extradata.begin(), audio_extradata.end());
    
    return result;
}

bool MediaHeader::deserialize(const uint8_t* data, size_t size, MediaHeader& header) {
    if (size < 4) return false;
    
    size_t offset = 0;
    
    auto readU32 = [&data, &offset]() -> uint32_t {
        uint32_t val = data[offset] | (data[offset+1] << 8) | 
                       (data[offset+2] << 16) | (data[offset+3] << 24);
        offset += 4;
        return val;
    };
    
    auto readI32 = [&data, &offset]() -> int32_t {
        int32_t val = data[offset] | (data[offset+1] << 8) | 
                      (data[offset+2] << 16) | (data[offset+3] << 24);
        offset += 4;
        return val;
    };
    
    auto readI64 = [&data, &offset]() -> int64_t {
        int64_t val = 0;
        for (int i = 0; i < 8; i++) {
            val |= static_cast<int64_t>(data[offset + i]) << (i * 8);
        }
        offset += 8;
        return val;
    };
    
    // Magic
    uint32_t magic = readU32();
    if (magic != MAGIC) {
        return false;
    }
    
    // Video info
    header.info.has_video = (data[offset++] != 0);
    header.info.video_width = readI32();
    header.info.video_height = readI32();
    header.info.video_pixel_format = static_cast<AVPixelFormat>(readI32());
    header.info.video_time_base.num = readI32();
    header.info.video_time_base.den = readI32();
    header.info.video_stream_index = readI32();
    header.info.video_codec_id = static_cast<AVCodecID>(readI32());
    header.info.video_codec_tag = readU32();
    
    // Audio info
    header.info.has_audio = (data[offset++] != 0);
    header.info.audio_sample_rate = readI32();
    header.info.audio_channels = readI32();
    header.info.audio_sample_format = static_cast<AVSampleFormat>(readI32());
    header.info.audio_time_base.num = readI32();
    header.info.audio_time_base.den = readI32();
    header.info.audio_stream_index = readI32();
    header.info.audio_codec_id = static_cast<AVCodecID>(readI32());
    header.info.audio_codec_tag = readU32();
    
    // Duration and bitrate
    header.info.duration = readI64();
    header.info.bit_rate = readI64();
    
    // Video extradata
    if (offset + 4 > size) return false;
    uint32_t video_extra_size = readU32();
    if (offset + video_extra_size > size) return false;
    header.video_extradata.assign(data + offset, data + offset + video_extra_size);
    offset += video_extra_size;
    
    // Audio extradata
    if (offset + 4 > size) return false;
    uint32_t audio_extra_size = readU32();
    if (offset + audio_extra_size > size) return false;
    header.audio_extradata.assign(data + offset, data + offset + audio_extra_size);
    offset += audio_extra_size;
    
    return true;
}

bool MediaHeader::deserialize(const std::vector<uint8_t>& data, MediaHeader& header) {
    return deserialize(data.data(), data.size(), header);
}

// ============================================================================
// MediaReader 实现
// ============================================================================

MediaReader::MediaReader() = default;

MediaReader::~MediaReader() {
    close();
}

bool MediaReader::open(const std::string& filename) {
    close();
    
    // 打开输入文件
    int ret = avformat_open_input(&format_ctx_, filename.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "无法打开文件: " << filename << " - " << errbuf << std::endl;
        return false;
    }
    
    // 获取流信息
    ret = avformat_find_stream_info(format_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "无法获取流信息" << std::endl;
        avformat_close_input(&format_ctx_);
        return false;
    }
    
    // 查找视频流和音频流
    for (unsigned int i = 0; i < format_ctx_->nb_streams; i++) {
        AVStream* stream = format_ctx_->streams[i];
        AVCodecParameters* codecpar = stream->codecpar;
        
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !media_info_.has_video) {
            media_info_.has_video = true;
            media_info_.video_width = codecpar->width;
            media_info_.video_height = codecpar->height;
            media_info_.video_pixel_format = static_cast<AVPixelFormat>(codecpar->format);
            media_info_.video_time_base = stream->time_base;
            media_info_.video_stream_index = i;
            media_info_.video_codec_id = codecpar->codec_id;
            media_info_.video_codec_tag = codecpar->codec_tag;
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && !media_info_.has_audio) {
            media_info_.has_audio = true;
            media_info_.audio_sample_rate = codecpar->sample_rate;
            media_info_.audio_channels = codecpar->ch_layout.nb_channels;
            media_info_.audio_sample_format = static_cast<AVSampleFormat>(codecpar->format);
            media_info_.audio_time_base = stream->time_base;
            media_info_.audio_stream_index = i;
            media_info_.audio_codec_id = codecpar->codec_id;
            media_info_.audio_codec_tag = codecpar->codec_tag;
        }
    }
    
    if (!media_info_.has_video && !media_info_.has_audio) {
        std::cerr << "未找到有效的音视频流" << std::endl;
        avformat_close_input(&format_ctx_);
        return false;
    }
    
    media_info_.duration = format_ctx_->duration;
    media_info_.bit_rate = format_ctx_->bit_rate;
    packet_count_ = 0;
    
    std::cout << "已打开媒体文件: " << filename << std::endl;
    if (media_info_.has_video) {
        std::cout << "  视频: " << media_info_.video_width << "x" << media_info_.video_height << std::endl;
    }
    if (media_info_.has_audio) {
        std::cout << "  音频: " << media_info_.audio_sample_rate << "Hz, " 
                  << media_info_.audio_channels << " channels" << std::endl;
    }
    
    return true;
}

void MediaReader::close() {
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
    }
    media_info_ = MediaInfo();
    packet_count_ = 0;
}

MediaHeader MediaReader::getMediaHeader() const {
    MediaHeader header;
    header.info = media_info_;
    
    // 获取视频额外数据（SPS/PPS等）
    if (format_ctx_ && media_info_.has_video && media_info_.video_stream_index >= 0) {
        AVStream* stream = format_ctx_->streams[media_info_.video_stream_index];
        if (stream->codecpar->extradata_size > 0) {
            header.video_extradata.assign(
                stream->codecpar->extradata,
                stream->codecpar->extradata + stream->codecpar->extradata_size
            );
        }
    }
    
    // 获取音频额外数据
    if (format_ctx_ && media_info_.has_audio && media_info_.audio_stream_index >= 0) {
        AVStream* stream = format_ctx_->streams[media_info_.audio_stream_index];
        if (stream->codecpar->extradata_size > 0) {
            header.audio_extradata.assign(
                stream->codecpar->extradata,
                stream->codecpar->extradata + stream->codecpar->extradata_size
            );
        }
    }
    
    return header;
}

bool MediaReader::readPacket(MediaPacket& packet) {
    if (!format_ctx_) {
        return false;
    }
    
    AVPacket* av_packet = av_packet_alloc();
    if (!av_packet) {
        return false;
    }
    
    int ret = av_read_frame(format_ctx_, av_packet);
    if (ret < 0) {
        av_packet_free(&av_packet);
        return false;
    }
    
    // 填充 MediaPacket
    packet.index = static_cast<uint32_t>(packet_count_++);
    packet.pts = av_packet->pts;
    packet.dts = av_packet->dts;
    packet.is_key_frame = (av_packet->flags & AV_PKT_FLAG_KEY) != 0;
    
    if (av_packet->stream_index == media_info_.video_stream_index) {
        packet.type = FrameType::VIDEO;
    } else if (av_packet->stream_index == media_info_.audio_stream_index) {
        packet.type = FrameType::AUDIO;
    } else {
        packet.type = FrameType::UNKNOWN;
    }
    
    // 复制数据
    packet.data.assign(av_packet->data, av_packet->data + av_packet->size);
    
    av_packet_free(&av_packet);
    return true;
}

bool MediaReader::seek(int64_t timestamp_us) {
    if (!format_ctx_) {
        return false;
    }
    
    int ret = av_seek_frame(format_ctx_, -1, timestamp_us, AVSEEK_FLAG_BACKWARD);
    return ret >= 0;
}

// ============================================================================
// MediaWriter 实现
// ============================================================================

MediaWriter::MediaWriter() = default;

MediaWriter::~MediaWriter() {
    close();
}

bool MediaWriter::initialize(const MediaHeader& header) {
    header_ = header;
    initialized_ = true;
    
    // 重置所有计数器和缓冲区
    packet_count_ = 0;
    video_packet_count_ = 0;
    audio_packet_count_ = 0;
    key_frame_count_ = 0;
    packet_buffer_.clear();
    
    std::cout << "写入器已初始化" << std::endl;
    if (header_.info.has_video) {
        std::cout << "  视频: " << header_.info.video_width << "x" 
                  << header_.info.video_height << std::endl;
    }
    if (header_.info.has_audio) {
        std::cout << "  音频: " << header_.info.audio_sample_rate << "Hz, "
                  << header_.info.audio_channels << " channels" << std::endl;
    }
    
    return true;
}

bool MediaWriter::open(const std::string& filename) {
    if (!initialized_) {
        std::cerr << "写入器未初始化" << std::endl;
        return false;
    }
    
    // 根据文件扩展名确定输出格式
    int ret = avformat_alloc_output_context2(&output_ctx_, nullptr, nullptr, filename.c_str());
    if (ret < 0 || !output_ctx_) {
        std::cerr << "无法创建输出上下文" << std::endl;
        return false;
    }
    
    // 添加视频流
    if (header_.info.has_video) {
        video_stream_ = avformat_new_stream(output_ctx_, nullptr);
        if (!video_stream_) {
            std::cerr << "无法创建视频流" << std::endl;
            avformat_free_context(output_ctx_);
            output_ctx_ = nullptr;
            return false;
        }
        
        video_stream_->time_base = header_.info.video_time_base;
        video_stream_->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        video_stream_->codecpar->codec_id = header_.info.video_codec_id;
        video_stream_->codecpar->codec_tag = header_.info.video_codec_tag;
        video_stream_->codecpar->width = header_.info.video_width;
        video_stream_->codecpar->height = header_.info.video_height;
        video_stream_->codecpar->format = header_.info.video_pixel_format;
        
        // 设置额外数据
        if (!header_.video_extradata.empty()) {
            video_stream_->codecpar->extradata_size = header_.video_extradata.size();
            video_stream_->codecpar->extradata = static_cast<uint8_t*>(
                av_malloc(header_.video_extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE)
            );
            std::memcpy(video_stream_->codecpar->extradata, 
                       header_.video_extradata.data(), 
                       header_.video_extradata.size());
        }
    }
    
    // 添加音频流
    if (header_.info.has_audio) {
        audio_stream_ = avformat_new_stream(output_ctx_, nullptr);
        if (!audio_stream_) {
            std::cerr << "无法创建音频流" << std::endl;
            avformat_free_context(output_ctx_);
            output_ctx_ = nullptr;
            return false;
        }
        
        audio_stream_->time_base = header_.info.audio_time_base;
        audio_stream_->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        audio_stream_->codecpar->codec_id = header_.info.audio_codec_id;
        audio_stream_->codecpar->codec_tag = header_.info.audio_codec_tag;
        audio_stream_->codecpar->sample_rate = header_.info.audio_sample_rate;
        audio_stream_->codecpar->format = header_.info.audio_sample_format;
        
        // 设置声道布局
        av_channel_layout_default(&audio_stream_->codecpar->ch_layout, 
                                  header_.info.audio_channels);
        
        // 设置额外数据
        if (!header_.audio_extradata.empty()) {
            audio_stream_->codecpar->extradata_size = header_.audio_extradata.size();
            audio_stream_->codecpar->extradata = static_cast<uint8_t*>(
                av_malloc(header_.audio_extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE)
            );
            std::memcpy(audio_stream_->codecpar->extradata,
                       header_.audio_extradata.data(),
                       header_.audio_extradata.size());
        }
    }
    
    // 打开输出文件
    if (!(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&output_ctx_->pb, filename.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "无法打开输出文件: " << errbuf << std::endl;
            avformat_free_context(output_ctx_);
            output_ctx_ = nullptr;
            return false;
        }
    }
    
    // 写入文件头
    ret = avformat_write_header(output_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "无法写入文件头: " << errbuf << std::endl;
        avio_closep(&output_ctx_->pb);
        avformat_free_context(output_ctx_);
        output_ctx_ = nullptr;
        return false;
    }
    
    header_written_ = true;
    
    // 重置计数器和缓冲区
    packet_count_ = 0;
    video_packet_count_ = 0;
    audio_packet_count_ = 0;
    key_frame_count_ = 0;
    packet_buffer_.clear();
    
    std::cout << "已打开输出文件: " << filename << std::endl;
    return true;
}

void MediaWriter::close() {
    if (output_ctx_) {
        if (header_written_) {
            av_write_trailer(output_ctx_);
            header_written_ = false;
        }
        
        if (!(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_ctx_->pb);
        }
        
        avformat_free_context(output_ctx_);
        output_ctx_ = nullptr;
    }
    
    video_stream_ = nullptr;
    audio_stream_ = nullptr;
}

bool MediaWriter::writePacket(const MediaPacket& packet) {
    if (!initialized_) {
        std::cerr << "写入器未初始化" << std::endl;
        return false;
    }
    
    // 检查包数据是否为空
    if (packet.data.empty()) {
        std::cerr << "警告: 收到空数据包" << std::endl;
        return false;
    }
    
    // 检查包类型
    if (packet.type != FrameType::VIDEO && packet.type != FrameType::AUDIO) {
        return false;
    }
    
    // 缓冲模式：将包添加到缓冲区，在 finalize() 时排序并写入
    if (buffering_mode_) {
        packet_buffer_.push_back(packet);
        packet_count_++;
        
        if (packet.type == FrameType::VIDEO) {
            video_packet_count_++;
        } else if (packet.type == FrameType::AUDIO) {
            audio_packet_count_++;
        }
        if (packet.is_key_frame) {
            key_frame_count_++;
        }
        
        return true;
    }
    
    // 直接写入模式
    return writePacketDirect(packet);
}

bool MediaWriter::writePacketDirect(const MediaPacket& packet) {
    if (!output_ctx_ || !header_written_) {
        std::cerr << "输出文件未打开" << std::endl;
        return false;
    }
    
    AVPacket* av_packet = av_packet_alloc();
    if (!av_packet) {
        std::cerr << "错误: 无法分配 AVPacket" << std::endl;
        return false;
    }
    
    // 分配数据缓冲区
    int ret = av_new_packet(av_packet, packet.data.size());
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "错误: 无法分配包数据缓冲区: " << errbuf << std::endl;
        av_packet_free(&av_packet);
        return false;
    }
    
    // 复制数据
    std::memcpy(av_packet->data, packet.data.data(), packet.data.size());
    
    // 设置时间戳
    av_packet->pts = packet.pts;
    av_packet->dts = packet.dts;
    
    if (packet.is_key_frame) {
        av_packet->flags |= AV_PKT_FLAG_KEY;
    }
    
    // 设置流索引
    if (packet.type == FrameType::VIDEO && video_stream_) {
        av_packet->stream_index = video_stream_->index;
    } else if (packet.type == FrameType::AUDIO && audio_stream_) {
        av_packet->stream_index = audio_stream_->index;
    } else {
        av_packet_free(&av_packet);
        return false;
    }
    
    // 写入
    ret = av_interleaved_write_frame(output_ctx_, av_packet);
    av_packet_free(&av_packet);
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        // 静默忽略错误，继续写入
        return false;
    }
    
    return true;
}

bool MediaWriter::finalize() {
    if (!output_ctx_) {
        std::cerr << "警告: 文件上下文不存在" << std::endl;
        return false;
    }
    
    if (!header_written_) {
        std::cerr << "警告: 文件头未写入，可能文件未正确打开" << std::endl;
        return false;
    }
    
    // 如果是缓冲模式，先将缓冲区中的包排序并写入
    if (buffering_mode_ && !packet_buffer_.empty()) {
        std::cout << "正在排序并写入 " << packet_buffer_.size() << " 个缓冲包..." << std::endl;
        
        // 按 index 排序（保持原始发送顺序）
        std::sort(packet_buffer_.begin(), packet_buffer_.end(),
            [](const MediaPacket& a, const MediaPacket& b) {
                return a.index < b.index;
            });
        
        uint64_t written = 0;
        uint64_t failed = 0;
        
        for (const auto& pkt : packet_buffer_) {
            if (writePacketDirect(pkt)) {
                written++;
            } else {
                failed++;
            }
            
            // 每写入 1000 个包显示进度
            if ((written + failed) % 1000 == 0) {
                std::cout << "\r写入进度: " << written << "/" << packet_buffer_.size() 
                          << " (失败: " << failed << ")        " << std::flush;
            }
        }
        
        std::cout << "\r写入完成: " << written << "/" << packet_buffer_.size() 
                  << " (失败: " << failed << ")        " << std::endl;
        
        packet_buffer_.clear();
    }
    
    // 刷新缓冲区，确保所有数据都写入磁盘
    if (!(output_ctx_->oformat->flags & AVFMT_NOFILE) && output_ctx_->pb) {
        avio_flush(output_ctx_->pb);
    }
    
    // 写入文件尾
    int ret = av_write_trailer(output_ctx_);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "错误: 写入文件尾失败: " << errbuf << std::endl;
        return false;
    }
    
    // 再次刷新，确保 trailer 写入磁盘
    if (!(output_ctx_->oformat->flags & AVFMT_NOFILE) && output_ctx_->pb) {
        avio_flush(output_ctx_->pb);
    }
    
    std::cout << "输出文件已完成:" << std::endl;
    std::cout << "  总包数: " << packet_count_ << std::endl;
    std::cout << "  视频包: " << video_packet_count_ << std::endl;
    std::cout << "  音频包: " << audio_packet_count_ << std::endl;
    std::cout << "  关键帧: " << key_frame_count_ << std::endl;
    
    if (video_packet_count_ > 0 && key_frame_count_ == 0) {
        std::cerr << "  警告: 没有关键帧，文件可能无法播放！" << std::endl;
    }
    
    header_written_ = false;  // 标记已写入 trailer，避免重复写入
    return true;
}

} // namespace AVCodecModule
