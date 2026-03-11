# Makefile for RaptorQ Multipath Stream
#
# 包含以下组件：
#   - 基础传输: sender_demo, receiver_demo
#   - 视频传输: video_streaming_demo

# 编译器和标志
CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC
INCLUDES = -I. -I./pack -I./network -I./event_base -I./libRaptorQ/src -I./VideoCodec
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
                      sender.cpp send_center.cpp receiver.cpp receiver_center.cpp
VIDEO_CODEC_SRC = VideoCodec/video_reader.cpp VideoCodec/video_writer.cpp

# ============================================
# 子模块对象文件
# ============================================
NETWORK_OBJS = network/build/network_server.o network/build/network_client.o
EVENT_OBJS = event_base/build/event_loop.o
PACK_LIB = pack/build/librqpack.a
VIDEO_CODEC_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(VIDEO_CODEC_SRC:.cpp=.o)))

# ============================================
# 目标文件
# ============================================
SENDER_OBJS = $(addprefix $(BUILD_DIR)/, $(SENDER_SRC:.cpp=.o))
RECEIVER_OBJS = $(addprefix $(BUILD_DIR)/, $(RECEIVER_SRC:.cpp=.o))
VIDEO_STREAMING_OBJS = $(addprefix $(BUILD_DIR)/, $(VIDEO_STREAMING_SRC:.cpp=.o))

# ============================================
# 可执行文件
# ============================================
SENDER_EXE = $(BUILD_DIR)/sender_demo
RECEIVER_EXE = $(BUILD_DIR)/receiver_demo
VIDEO_STREAMING_EXE = $(BUILD_DIR)/video_streaming_demo

# ============================================
# 默认目标
# ============================================
.PHONY: all
all: check-deps $(BUILD_DIR) $(SENDER_EXE) $(RECEIVER_EXE) $(VIDEO_STREAMING_EXE)
	@echo ""
	@echo "=========================================="
	@echo "编译完成！"
	@echo "=========================================="
	@echo "基础传输:"
	@echo "  发送端: $(SENDER_EXE)"
	@echo "  接收端: $(RECEIVER_EXE)"
	@echo "视频传输:"
	@echo "  收发端: $(VIDEO_STREAMING_EXE)"
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

# VideoCodec 模块编译
$(BUILD_DIR)/video_reader.o: VideoCodec/video_reader.cpp VideoCodec/video_reader.h VideoCodec/video_codec.h
	@echo "编译 video_reader.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/video_writer.o: VideoCodec/video_writer.cpp VideoCodec/video_writer.h VideoCodec/video_codec.h
	@echo "编译 video_writer.cpp..."
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
	@$(MAKE) -C VideoCodec clean
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
	@echo "或者直接运行:"
	@echo "  ./build/video_streaming_demo receiver 9001 output.mp4"
	@echo "  ./build/video_streaming_demo sender 127.0.0.1 9001 input.mp4"

# ============================================
# 单独编译目标
# ============================================
.PHONY: sender receiver video
sender: check-deps $(BUILD_DIR) $(SENDER_EXE)
receiver: check-deps $(BUILD_DIR) $(RECEIVER_EXE)
video: check-deps $(BUILD_DIR) $(VIDEO_STREAMING_EXE)
