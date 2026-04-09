# Makefile for RaptorQ Multipath Stream
#
# 包含以下组件：
#   - 基础传输: sender_demo, receiver_demo
#   - 视频传输: video_streaming_demo

# 编译器和标志
CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC
INCLUDES = -I. -I./pack -I./network -I./event_base -I./libRaptorQ/src -I./VideoCodec -I./VoiceCodec
# -rpath 让运行时从项目内找到 libRaptorQ，无需设置 LD_LIBRARY_PATH
LDFLAGS = -L./libRaptorQ/build/lib -Wl,-rpath,'$$ORIGIN/../libRaptorQ/build/lib'

# 库
LIBS = -lRaptorQ -lpthread

# 自动检测 libevent 和 ffmpeg 路径
LIBEVENT_CFLAGS := $(shell pkg-config --cflags libevent 2>/dev/null || echo "-I/usr/include")
LIBEVENT_LIBS := $(shell pkg-config --libs libevent 2>/dev/null || echo "-levent -levent_core -levent_pthreads")
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavcodec libavformat libavutil 2>/dev/null || echo "")
FFMPEG_LIBS := $(shell pkg-config --libs libavcodec libavformat libavutil 2>/dev/null || echo "-lavcodec -lavformat -lavutil")

INCLUDES += $(LIBEVENT_CFLAGS) $(FFMPEG_CFLAGS)
LDFLAGS  += $(shell pkg-config --libs --libs-only-L libevent 2>/dev/null || echo "-L/usr/lib")
LIBS    += $(LIBEVENT_LIBS) $(FFMPEG_LIBS)

# 构建目录
BUILD_DIR = build

# ============================================
# 基础传输 - 源文件
# ============================================
SENDER_SRC = sender_demo.cpp sender.cpp send_center.cpp
RECEIVER_SRC = receiver_demo.cpp receiver.cpp receiver_center.cpp

# ============================================
# 视频传输 - 源文件
# ============================================
VIDEO_STREAMING_SRC = video_streaming_demo.cpp video_transmitter.cpp video_receiver.cpp \
                      sender.cpp send_center.cpp receiver.cpp receiver_center.cpp \
                      unified_sender.cpp unified_receiver.cpp send_buffer.cpp scheduler.cpp \
                      block_partition.cpp feedback.cpp reorder_buffer.cpp fc_control.cpp
VIDEO_CODEC_SRC = VideoCodec/video_reader.cpp VideoCodec/video_writer.cpp

# ============================================
# 语音传输 - 源文件
# ============================================
VOICE_STREAMING_SRC = voice_demo.cpp voice_transmitter.cpp voice_receiver.cpp \
                      sender.cpp send_center.cpp receiver.cpp receiver_center.cpp
VOICE_CODEC_SRC = VoiceCodec/voice_codec.cpp VoiceCodec/voice_reader.cpp

# 多数据流传输 - 源文件
# ============================================
MULTI_STREAMING_SRC = multi_streaming_demo.cpp video_transmitter.cpp video_receiver.cpp \
                      fc_control.cpp point_cloud.cpp grid_map.cpp \
                      sender.cpp send_center.cpp receiver.cpp receiver_center.cpp \
                      unified_sender.cpp unified_receiver.cpp send_buffer.cpp scheduler.cpp \
                      block_partition.cpp feedback.cpp reorder_buffer.cpp

# UnifiedReceiver 测试 - 源文件
UNIFIED_RECEIVER_TEST_SRC = unified_receiver_test.cpp unified_receiver.cpp unified_sender.cpp \
                            receiver.cpp receiver_center.cpp \
                            fc_control.cpp point_cloud.cpp grid_map.cpp \
                            sender.cpp send_center.cpp \
                            send_buffer.cpp scheduler.cpp block_partition.cpp feedback.cpp reorder_buffer.cpp

# BlockPartition 聚合/拆分测试 - 源文件
BLOCK_PARTITION_TEST_SRC = block_partition_test.cpp unified_sender.cpp unified_receiver.cpp \
                           receiver.cpp receiver_center.cpp \
                           sender.cpp send_center.cpp \
                           send_buffer.cpp scheduler.cpp block_partition.cpp feedback.cpp reorder_buffer.cpp

# 多流并发测试 - 源文件
MULTI_STREAM_TEST_SRC = multi_stream_test.cpp unified_sender.cpp unified_receiver.cpp \
                        receiver.cpp receiver_center.cpp \
                        sender.cpp send_center.cpp \
                        send_buffer.cpp scheduler.cpp block_partition.cpp feedback.cpp reorder_buffer.cpp

# SendBuffer 测试 - 源文件
SENDBUFFER_TEST_SRC = send_buffer_test.cpp send_buffer.cpp

# Scheduler 测试 - 源文件
SCHEDULER_TEST_SRC = scheduler_test.cpp scheduler.cpp send_buffer.cpp sender.cpp send_center.cpp

# ReorderBuffer 测试 - 源文件
REORDER_TEST_SRC = reorder_buffer_test.cpp reorder_buffer.cpp send_buffer.cpp

# SendBuffer + ReorderBuffer 同步测试
SYNC_TEST_SRC = test_send_receive_sync.cpp send_buffer.cpp reorder_buffer.cpp

# 集成测试（真实网络）
INTEGRATED_TEST_SRC = integrated_test.cpp send_buffer.cpp scheduler.cpp sender.cpp send_center.cpp

# Feedback 模块测试
FEEDBACK_TEST_SRC = feedback_test.cpp feedback.cpp send_buffer.cpp

# ============================================
# 子模块对象文件
# ============================================
NETWORK_OBJS = network/build/network_server.o network/build/network_client.o
EVENT_OBJS = event_base/build/event_loop.o
PACK_LIB = pack/build/librqpack.a
VIDEO_CODEC_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(VIDEO_CODEC_SRC:.cpp=.o)))
VOICE_CODEC_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(VOICE_CODEC_SRC:.cpp=.o)))

# ============================================
# 目标文件
# ============================================
SENDER_OBJS = $(addprefix $(BUILD_DIR)/, $(SENDER_SRC:.cpp=.o))
RECEIVER_OBJS = $(addprefix $(BUILD_DIR)/, $(RECEIVER_SRC:.cpp=.o))
VIDEO_STREAMING_OBJS = $(addprefix $(BUILD_DIR)/, $(VIDEO_STREAMING_SRC:.cpp=.o))
VOICE_STREAMING_OBJS = $(addprefix $(BUILD_DIR)/, $(VOICE_STREAMING_SRC:.cpp=.o))
MULTI_STREAMING_OBJS = $(addprefix $(BUILD_DIR)/, $(MULTI_STREAMING_SRC:.cpp=.o))
UNIFIED_RECEIVER_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(UNIFIED_RECEIVER_TEST_SRC:.cpp=.o))
BLOCK_PARTITION_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(BLOCK_PARTITION_TEST_SRC:.cpp=.o))
MULTI_STREAM_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(MULTI_STREAM_TEST_SRC:.cpp=.o))
SENDBUFFER_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(SENDBUFFER_TEST_SRC:.cpp=.o))
SCHEDULER_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(SCHEDULER_TEST_SRC:.cpp=.o))
REORDER_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(REORDER_TEST_SRC:.cpp=.o))
SYNC_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(SYNC_TEST_SRC:.cpp=.o))
INTEGRATED_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(INTEGRATED_TEST_SRC:.cpp=.o))
FEEDBACK_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(FEEDBACK_TEST_SRC:.cpp=.o))

# ============================================
# 可执行文件
# ============================================
SENDER_EXE = $(BUILD_DIR)/sender_demo
RECEIVER_EXE = $(BUILD_DIR)/receiver_demo
VIDEO_STREAMING_EXE = $(BUILD_DIR)/video_streaming_demo
VOICE_STREAMING_EXE = $(BUILD_DIR)/voice_demo
MULTI_STREAMING_EXE = $(BUILD_DIR)/multi_streaming_demo
UNIFIED_RECEIVER_TEST_EXE = $(BUILD_DIR)/unified_receiver_test
MULTI_STREAM_TEST_EXE = $(BUILD_DIR)/multi_stream_test
BLOCK_PARTITION_TEST_EXE = $(BUILD_DIR)/block_partition_test
SENDBUFFER_TEST_EXE = $(BUILD_DIR)/send_buffer_test
SCHEDULER_TEST_EXE = $(BUILD_DIR)/scheduler_test
REORDER_TEST_EXE = $(BUILD_DIR)/reorder_buffer_test
SYNC_TEST_EXE = $(BUILD_DIR)/test_sync
INTEGRATED_TEST_EXE = $(BUILD_DIR)/integrated_test
FEEDBACK_TEST_EXE = $(BUILD_DIR)/feedback_test

# ============================================
# 默认目标
# ============================================
.PHONY: all
all: check-deps $(BUILD_DIR) $(SENDER_EXE) $(RECEIVER_EXE) $(VIDEO_STREAMING_EXE) $(VOICE_STREAMING_EXE) $(MULTI_STREAMING_EXE) $(UNIFIED_RECEIVER_TEST_EXE) $(SENDBUFFER_TEST_EXE) $(SCHEDULER_TEST_EXE) $(REORDER_TEST_EXE) $(SYNC_TEST_EXE) $(INTEGRATED_TEST_EXE) $(FEEDBACK_TEST_EXE)
	@echo ""
	@echo "=========================================="
	@echo "编译完成！"
	@echo "=========================================="
	@echo "基础传输:"
	@echo "  发送端: $(SENDER_EXE)"
	@echo "  接收端: $(RECEIVER_EXE)"
	@echo "视频传输:"
	@echo "  收发端: $(VIDEO_STREAMING_EXE)"
	@echo "语音传输:"
	@echo "  收发端: $(VOICE_STREAMING_EXE)"
	@echo "多数据流传输:"
	@echo "  统一入口: $(MULTI_STREAMING_EXE)"
	@echo "UnifiedReceiver测试:"
	@echo "  接收测试: $(UNIFIED_RECEIVER_TEST_EXE)"
	@echo "=========================================="

# 创建构建目录
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# ============================================
# 基础传输 - 编译规则
# ============================================
$(SENDER_EXE): $(SENDER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接基础发送端..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@echo "✓ 基础发送端编译完成"

$(RECEIVER_EXE): $(RECEIVER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接基础接收端..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@echo "✓ 基础接收端编译完成"

$(BUILD_DIR)/sender.o: sender.cpp sender.h common.h
	@echo "编译 sender.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/send_center.o: send_center.cpp send_center.h sender.h
	@echo "编译 send_center.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/sender_demo.o: sender_demo.cpp send_center.h
	@echo "编译 sender_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/receiver.o: receiver.cpp receiver.h common.h
	@echo "编译 receiver.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/receiver_center.o: receiver_center.cpp receiver_center.h receiver.h
	@echo "编译 receiver_center.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/receiver_demo.o: receiver_demo.cpp receiver_center.h
	@echo "编译 receiver_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# 视频传输 - 编译规则
# ============================================
$(VIDEO_STREAMING_EXE): $(VIDEO_STREAMING_OBJS) $(VIDEO_CODEC_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接视频传输程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@echo "✓ 视频传输程序编译完成"

$(BUILD_DIR)/video_streaming_demo.o: video_streaming_demo.cpp video_transmitter.h video_receiver.h video_common.h
	@echo "编译 video_streaming_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/video_transmitter.o: video_transmitter.cpp video_transmitter.h video_common.h
	@echo "编译 video_transmitter.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/video_receiver.o: video_receiver.cpp video_receiver.h video_common.h
	@echo "编译 video_receiver.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# UnifiedSender/Receiver 模块编译
$(BUILD_DIR)/unified_sender.o: unified_sender.cpp unified_sender.h send_buffer.h scheduler.h block_partition.h feedback.h
	@echo "编译 unified_sender.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/unified_receiver.o: unified_receiver.cpp unified_receiver.h reorder_buffer.h
	@echo "编译 unified_receiver.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/send_buffer.o: send_buffer.cpp send_buffer.h
	@echo "编译 send_buffer.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/scheduler.o: scheduler.cpp scheduler.h
	@echo "编译 scheduler.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/block_partition.o: block_partition.cpp block_partition.h
	@echo "编译 block_partition.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/reorder_buffer.o: reorder_buffer.cpp reorder_buffer.h
	@echo "编译 reorder_buffer.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# VideoCodec 模块编译
$(BUILD_DIR)/video_reader.o: VideoCodec/video_reader.cpp VideoCodec/video_reader.h VideoCodec/video_codec.h
	@echo "编译 video_reader.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/video_writer.o: VideoCodec/video_writer.cpp VideoCodec/video_writer.h VideoCodec/video_codec.h
	@echo "编译 video_writer.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# 语音传输 - 编译规则
# ============================================
$(VOICE_STREAMING_EXE): $(VOICE_STREAMING_OBJS) $(VOICE_CODEC_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接语音传输程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@echo "✓ 语音传输程序编译完成"

$(BUILD_DIR)/voice_demo.o: voice_demo.cpp voice_transmitter.h voice_receiver.h
	@echo "编译 voice_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/voice_transmitter.o: voice_transmitter.cpp voice_transmitter.h data_common.h
	@echo "编译 voice_transmitter.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/voice_receiver.o: voice_receiver.cpp voice_receiver.h data_common.h
	@echo "编译 voice_receiver.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/voice_codec.o: VoiceCodec/voice_codec.cpp VoiceCodec/voice_codec.h
	@echo "编译 voice_codec.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/voice_reader.o: VoiceCodec/voice_reader.cpp VoiceCodec/voice_reader.h
	@echo "编译 voice_reader.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# 多数据流传输 - 编译规则
# ============================================
$(MULTI_STREAMING_EXE): $(MULTI_STREAMING_OBJS) $(VIDEO_CODEC_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接多数据流传输程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@echo "✓ 多数据流传输程序编译完成"

$(BUILD_DIR)/multi_streaming_demo.o: multi_streaming_demo.cpp data_common.h video_transmitter.h video_receiver.h fc_control.h point_cloud.h grid_map.h
	@echo "编译 multi_streaming_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# UnifiedReceiver 测试 - 编译规则
# ============================================
$(UNIFIED_RECEIVER_TEST_EXE): $(UNIFIED_RECEIVER_TEST_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接 UnifiedReceiver 测试程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@echo "✓ UnifiedReceiver 测试程序编译完成"

$(BUILD_DIR)/unified_receiver_test.o: unified_receiver_test.cpp unified_receiver.h data_common.h
	@echo "编译 unified_receiver_test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/unified_receiver.o: unified_receiver.cpp unified_receiver.h receiver.h
	@echo "编译 unified_receiver.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# SendBuffer 测试 - 编译规则
# ============================================
$(SENDBUFFER_TEST_EXE): $(SENDBUFFER_TEST_OBJS)
	@echo "链接 SendBuffer 测试程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread
	@echo "✓ SendBuffer 测试程序编译完成"

$(BUILD_DIR)/send_buffer_test.o: send_buffer_test.cpp send_buffer.h stream_config.h
	@echo "编译 send_buffer_test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/send_buffer.o: send_buffer.cpp send_buffer.h
	@echo "编译 send_buffer.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# Scheduler 测试 - 编译规则
# ============================================
$(SCHEDULER_TEST_EXE): $(SCHEDULER_TEST_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接 Scheduler 测试程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS) -lpthread
	@echo "✓ Scheduler 测试程序编译完成"

$(BUILD_DIR)/scheduler_test.o: scheduler_test.cpp scheduler.h send_buffer.h
	@echo "编译 scheduler_test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/scheduler.o: scheduler.cpp scheduler.h
	@echo "编译 scheduler.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# ReorderBuffer 测试 - 编译规则
# ============================================
$(REORDER_TEST_EXE): $(REORDER_TEST_OBJS)
	@echo "链接 ReorderBuffer 测试程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread
	@echo "✓ ReorderBuffer 测试程序编译完成"

$(BUILD_DIR)/reorder_buffer_test.o: reorder_buffer_test.cpp reorder_buffer.h
	@echo "编译 reorder_buffer_test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/reorder_buffer.o: reorder_buffer.cpp reorder_buffer.h
	@echo "编译 reorder_buffer.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# Send+Receive 同步测试 - 编译规则
# ============================================
$(SYNC_TEST_EXE): $(SYNC_TEST_OBJS)
	@echo "链接 Send+Receive 同步测试程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread
	@echo "✓ Send+Receive 同步测试程序编译完成"

$(BUILD_DIR)/test_send_receive_sync.o: test_send_receive_sync.cpp send_buffer.h reorder_buffer.h
	@echo "编译 test_send_receive_sync.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# 集成测试（真实网络）- 编译规则
# ============================================
$(INTEGRATED_TEST_EXE): $(INTEGRATED_TEST_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接集成测试程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS) -lpthread
	@echo "✓ 集成测试程序编译完成"

$(BUILD_DIR)/integrated_test.o: integrated_test.cpp send_buffer.h scheduler.h sender.h
	@echo "编译 integrated_test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# Feedback 测试 - 编译规则
# ============================================
$(FEEDBACK_TEST_EXE): $(FEEDBACK_TEST_OBJS)
	@echo "链接 Feedback 测试程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread
	@echo "✓ Feedback 测试程序编译完成"

$(BUILD_DIR)/feedback_test.o: feedback_test.cpp feedback.h
	@echo "编译 feedback_test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/feedback.o: feedback.cpp feedback.h
	@echo "编译 feedback.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/fc_control.o: fc_control.cpp fc_control.h data_common.h
	@echo "编译 fc_control.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/point_cloud.o: point_cloud.cpp point_cloud.h data_common.h
	@echo "编译 point_cloud.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/grid_map.o: grid_map.cpp grid_map.h data_common.h
	@echo "编译 grid_map.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ============================================
# 依赖检查和子模块构建
# ============================================
.PHONY: check-deps
check-deps:
	@echo "检查依赖..."
	@command -v $(CXX) >/dev/null 2>&1 || { echo "错误: 未找到 g++"; exit 1; }
	@pkg-config --exists libevent || { echo "错误: 未找到 libevent (请安装: sudo apt install libevent-dev)"; exit 1; }
	@pkg-config --exists libavcodec libavformat libavutil || { echo "警告: 未找到 FFmpeg (视频功能需要)"; }
	@echo "✓ 依赖检查通过"
	@echo ""
	@echo "构建子模块..."
	@$(MAKE) -C event_base all
	@$(MAKE) -C network all
	@$(MAKE) -C pack all

.PHONY: rebuild-deps
rebuild-deps:
	@echo "重新构建所有子模块..."
	@$(MAKE) -C event_base clean && $(MAKE) -C event_base all
	@$(MAKE) -C network clean && $(MAKE) -C network all
	@$(MAKE) -C pack clean && $(MAKE) -C pack all
	@echo "✓ 子模块重新构建完成"

# ============================================
# 测试运行
# ============================================
.PHONY: run-sender
run-sender: $(SENDER_EXE)
	@echo "运行基础发送端..."
	$(SENDER_EXE) 127.0.0.1 9000

.PHONY: run-receiver
run-receiver: $(RECEIVER_EXE)
	@echo "运行基础接收端..."
	$(RECEIVER_EXE) 9000

.PHONY: run-video-receiver
run-video-receiver: $(VIDEO_STREAMING_EXE)
	@echo "运行视频接收端..."
	$(VIDEO_STREAMING_EXE) receiver 9001 output/received_video.mp4

.PHONY: run-video-sender
run-video-sender: $(VIDEO_STREAMING_EXE)
	@echo "运行视频发送端..."
	$(VIDEO_STREAMING_EXE) sender 127.0.0.1 9001 data/videos/test.mp4

# ============================================
# 清理
# ============================================
.PHONY: clean
clean:
	@echo "清理构建文件..."
	rm -rf $(BUILD_DIR)
	@echo "✓ 清理完成"

.PHONY: distclean
distclean: clean
	@echo "清理所有子模块..."
	@$(MAKE) -C pack clean
	@$(MAKE) -C network clean
	@$(MAKE) -C event_base clean
	@$(MAKE) -C VideoCodec clean 2>/dev/null || true
	@$(MAKE) -C VoiceCodec clean 2>/dev/null || true
	@echo "✓ 深度清理完成"

# ============================================
# 帮助
# ============================================
.PHONY: help
help:
	@echo "RaptorQ Multipath Stream Makefile"
	@echo ""
	@echo "主要目标:"
	@echo "  make all              - 编译所有程序（默认）"
	@echo "  make clean            - 清理构建文件"
	@echo "  make distclean        - 深度清理（包括子模块）"
	@echo ""
	@echo "基础传输测试:"
	@echo "  终端1: make run-receiver"
	@echo "  终端2: make run-sender"
	@echo ""
	@echo "视频传输测试:"
	@echo "  终端1: make run-video-receiver"
	@echo "  终端2: make run-video-sender"
	@echo ""
	@echo "语音传输测试:"
	@echo "  终端1: ./build/voice_demo receive 9004 output/voice/received.wav"
	@echo "  终端2: ./build/voice_demo send 127.0.0.1 9004 data/voice/test.wav"
	@echo ""
	@echo "多数据流传输:"
	@echo "  视频:    ./build/multi_streaming_demo receiver video 9001 output.mp4"
	@echo "           ./build/multi_streaming_demo sender video 127.0.0.1 9001 input.mp4"
	@echo "  飞控:    ./build/multi_streaming_demo receiver fc 9000"
	@echo "           ./build/multi_streaming_demo sender fc 127.0.0.1 9000"
	@echo "  点云:    ./build/multi_streaming_demo receiver pointcloud 9002"
	@echo "           ./build/multi_streaming_demo sender pointcloud 127.0.0.1 9002 test"
	@echo "  栅格:    ./build/multi_streaming_demo receiver gridmap 9003"
	@echo "           ./build/multi_streaming_demo sender gridmap 127.0.0.1 9003 test"
	@echo "  语音:    ./build/voice_demo receive 9004 output/voice/received.wav"
	@echo "           ./build/voice_demo send 127.0.0.1 9004 data/voice/test.wav"

# ============================================
# 单独编译目标
# ============================================
.PHONY: sender receiver video voice multi sendbuffer scheduler reorder sync integrated feedback
sender: check-deps $(BUILD_DIR) $(SENDER_EXE)
receiver: check-deps $(BUILD_DIR) $(RECEIVER_EXE)
video: check-deps $(BUILD_DIR) $(VIDEO_STREAMING_EXE)
voice: check-deps $(BUILD_DIR) $(VOICE_STREAMING_EXE)
multi: check-deps $(BUILD_DIR) $(MULTI_STREAMING_EXE)
unified-test: check-deps $(BUILD_DIR) $(UNIFIED_RECEIVER_TEST_EXE)
sendbuffer: check-deps $(BUILD_DIR) $(SENDBUFFER_TEST_EXE)
scheduler: check-deps $(BUILD_DIR) $(SCHEDULER_TEST_EXE)
reorder: check-deps $(BUILD_DIR) $(REORDER_TEST_EXE)
sync: check-deps $(BUILD_DIR) $(SYNC_TEST_EXE)
integrated: check-deps $(BUILD_DIR) $(INTEGRATED_TEST_EXE)
feedback: check-deps $(BUILD_DIR) $(FEEDBACK_TEST_EXE)

# ============================================
# 样例数据生成工具
# ============================================
GEN_DATA_SRC = generate_sample_data.cpp
GEN_DATA_OBJS = $(addprefix $(BUILD_DIR)/, $(GEN_DATA_SRC:.cpp=.o))
GEN_DATA_EXE = $(BUILD_DIR)/generate_sample_data

.PHONY: gen-data

gen-data: $(GEN_DATA_EXE)
	@echo "生成样例数据..."
	@./$(GEN_DATA_EXE)

$(GEN_DATA_EXE): $(GEN_DATA_OBJS) $(PACK_LIB)
	@echo "链接样例数据生成工具..."
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@echo "✓ 样例数据生成工具编译完成"

$(BUILD_DIR)/generate_sample_data.o: generate_sample_data.cpp data_common.h
	@echo "编译 generate_sample_data.cpp..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# BlockPartition 聚合/拆分测试
$(BLOCK_PARTITION_TEST_EXE): $(BLOCK_PARTITION_TEST_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接 BlockPartition 测试程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
	@echo "✓ BlockPartition 测试程序编译完成"

$(BUILD_DIR)/block_partition_test.o: block_partition_test.cpp unified_sender.h unified_receiver.h
	@echo "编译 block_partition_test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 多数据流并发传输测试
$(MULTI_STREAM_TEST_EXE): $(MULTI_STREAM_TEST_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB) $(VIDEO_CODEC_OBJS) $(VOICE_CODEC_OBJS)
	@echo "链接多数据流并发测试程序..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS) -lpthread
	@echo "✓ 多数据流并发测试程序编译完成"

$(BUILD_DIR)/multi_stream_test.o: multi_stream_test.cpp unified_sender.h unified_receiver.h \
        voice_transmitter.h video_transmitter.h point_cloud.h grid_map.h fc_control.h
	@echo "编译 multi_stream_test.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: blockpartition
blockpartition: check-deps $(BUILD_DIR) $(BLOCK_PARTITION_TEST_EXE)

.PHONY: multistream
multistream: check-deps $(BUILD_DIR) $(MULTI_STREAM_TEST_EXE)
