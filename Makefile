# Makefile for RaptorQStream Demo
#
# 模块结构:
#   av_codec/   - 音视频编解码 + 基础网络传输
#   pack/       - RaptorQ FEC 编码
#   network/    - 网络传输
#   event_base/ - 事件循环
#
# Demo:
#   基础版本 (无FEC):
#     sender_demo      - 基础数据发送
#     receiver_demo    - 基础数据接收
#     av_codec/av_sender_demo   - 基础音视频发送
#     av_codec/av_receiver_demo - 基础音视频接收
#
#   带FEC版本:
#     av_sender_demo   - 音视频 + RaptorQ FEC 发送
#     av_receiver_demo - 音视频 + RaptorQ FEC 接收

# 编译器和标志
CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC
INCLUDES = -I. -I./pack -I./network -I./event_base -I./libRaptorQ/src -I./av_codec
LDFLAGS = -L./libRaptorQ/build/lib

# 库
LIBS = -lRaptorQ -lpthread

# 自动检测 libevent 路径
LIBEVENT_PREFIX := $(shell brew --prefix libevent 2>/dev/null || echo "/usr/local")
INCLUDES += -I$(LIBEVENT_PREFIX)/include
LDFLAGS += -L$(LIBEVENT_PREFIX)/lib
LIBS += -levent -levent_core

# FFmpeg 配置
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavcodec libavformat libswscale libswresample libavutil 2>/dev/null)
FFMPEG_LIBS := $(shell pkg-config --libs libavcodec libavformat libswscale libswresample libavutil 2>/dev/null)

ifeq ($(FFMPEG_LIBS),)
    FFMPEG_PREFIX := $(shell brew --prefix ffmpeg 2>/dev/null || echo "/usr/local")
    FFMPEG_CFLAGS = -I$(FFMPEG_PREFIX)/include
    FFMPEG_LIBS = -L$(FFMPEG_PREFIX)/lib -lavcodec -lavformat -lswscale -lswresample -lavutil
endif

# 构建目录
BUILD_DIR = build

# 子模块
NETWORK_OBJS = network/build/network_server.o network/build/network_client.o
EVENT_OBJS = event_base/build/event_loop.o
PACK_LIB = pack/build/librqpack.a
AV_CODEC_LIB = av_codec/build/libavcodec_module.a

# 基础 Demo 源文件
SENDER_SRC = sender_demo.cpp sender.cpp send_center.cpp
RECEIVER_SRC = receiver_demo.cpp receiver.cpp receiver_center.cpp

# 带FEC的音视频 Demo 源文件
AV_SENDER_SRC = av_sender_demo.cpp sender.cpp send_center.cpp
AV_RECEIVER_SRC = av_receiver_demo.cpp receiver.cpp receiver_center.cpp

# 目标文件
SENDER_OBJS = $(addprefix $(BUILD_DIR)/, $(SENDER_SRC:.cpp=.o))
RECEIVER_OBJS = $(addprefix $(BUILD_DIR)/, $(RECEIVER_SRC:.cpp=.o))
AV_SENDER_OBJS = $(addprefix $(BUILD_DIR)/, $(AV_SENDER_SRC:.cpp=.o))
AV_RECEIVER_OBJS = $(addprefix $(BUILD_DIR)/, $(AV_RECEIVER_SRC:.cpp=.o))

# 可执行文件
SENDER_EXE = $(BUILD_DIR)/sender_demo
RECEIVER_EXE = $(BUILD_DIR)/receiver_demo
AV_SENDER_EXE = $(BUILD_DIR)/av_sender_demo
AV_RECEIVER_EXE = $(BUILD_DIR)/av_receiver_demo

# ============================================================================
# 目标
# ============================================================================

.PHONY: all
all: check-deps $(BUILD_DIR) $(SENDER_EXE) $(RECEIVER_EXE)
	@echo ""
	@echo "=========================================="
	@echo "基础版本编译完成！"
	@echo "  发送端: $(SENDER_EXE)"
	@echo "  接收端: $(RECEIVER_EXE)"
	@echo "=========================================="
	@echo ""
	@echo "其他目标:"
	@echo "  make av      - 编译带FEC的音视频版本"
	@echo "  make av-base - 编译基础音视频版本(无FEC)"

# 带FEC的音视频版本
.PHONY: av
av: check-deps check-ffmpeg $(BUILD_DIR) build-av-codec $(AV_SENDER_EXE) $(AV_RECEIVER_EXE)
	@echo ""
	@echo "=========================================="
	@echo "音视频版本编译完成（带 RaptorQ FEC）！"
	@echo "  发送端: $(AV_SENDER_EXE)"
	@echo "  接收端: $(AV_RECEIVER_EXE)"
	@echo "=========================================="

# 基础音视频版本（无FEC，在av_codec目录中）
.PHONY: av-base
av-base:
	@echo "编译基础音视频版本（无FEC）..."
	@$(MAKE) -C av_codec demo
	@echo ""
	@echo "=========================================="
	@echo "基础音视频版本编译完成（无FEC）！"
	@echo "  发送端: av_codec/build/av_sender_demo"
	@echo "  接收端: av_codec/build/av_receiver_demo"
	@echo "=========================================="

# 编译所有版本
.PHONY: all-versions
all-versions: all av av-base
	@echo ""
	@echo "=========================================="
	@echo "所有版本编译完成！"
	@echo "=========================================="

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# ============================================================================
# 基础版本链接
# ============================================================================

$(SENDER_EXE): $(SENDER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接发送端..."
	$(CXX) $(CXXFLAGS) -o $@ $(SENDER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB) $(LDFLAGS) $(LIBS)
	@echo "✓ 发送端编译完成: $@"

$(RECEIVER_EXE): $(RECEIVER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接接收端..."
	$(CXX) $(CXXFLAGS) -o $@ $(RECEIVER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB) $(LDFLAGS) $(LIBS)
	@echo "✓ 接收端编译完成: $@"

# ============================================================================
# 带FEC音视频版本链接
# ============================================================================

$(AV_SENDER_EXE): $(AV_SENDER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB) $(AV_CODEC_LIB)
	@echo "链接音视频发送端（带FEC）..."
	$(CXX) $(CXXFLAGS) -o $@ $(AV_SENDER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB) $(AV_CODEC_LIB) $(LDFLAGS) $(LIBS) $(FFMPEG_LIBS)
	@echo "✓ 音视频发送端编译完成: $@"

$(AV_RECEIVER_EXE): $(AV_RECEIVER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB) $(AV_CODEC_LIB)
	@echo "链接音视频接收端（带FEC）..."
	$(CXX) $(CXXFLAGS) -o $@ $(AV_RECEIVER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB) $(AV_CODEC_LIB) $(LDFLAGS) $(LIBS) $(FFMPEG_LIBS)
	@echo "✓ 音视频接收端编译完成: $@"

# ============================================================================
# 编译规则
# ============================================================================

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

$(BUILD_DIR)/av_sender_demo.o: av_sender_demo.cpp send_center.h av_codec/av_codec.h
	@echo "编译 av_sender_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(FFMPEG_CFLAGS) -c $< -o $@

$(BUILD_DIR)/av_receiver_demo.o: av_receiver_demo.cpp receiver_center.h av_codec/av_codec.h
	@echo "编译 av_receiver_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(FFMPEG_CFLAGS) -c $< -o $@

# ============================================================================
# 子模块构建
# ============================================================================

.PHONY: build-av-codec
build-av-codec:
	@echo "构建 av_codec 模块..."
	@$(MAKE) -C av_codec all

.PHONY: check-deps
check-deps:
	@echo "检查依赖..."
	@command -v $(CXX) >/dev/null 2>&1 || { echo "错误: 未找到 g++"; exit 1; }
	@test -d $(LIBEVENT_PREFIX)/include || { echo "错误: 未找到 libevent (尝试: brew install libevent)"; exit 1; }
	@echo "✓ 依赖检查通过"
	@echo ""
	@echo "构建子模块..."
	@$(MAKE) -C event_base all
	@$(MAKE) -C network all
	@$(MAKE) -C pack all

.PHONY: check-ffmpeg
check-ffmpeg:
	@echo "检查 FFmpeg 依赖..."
	@pkg-config --exists libavcodec 2>/dev/null || \
		(test -d "$$(brew --prefix ffmpeg 2>/dev/null)/include" 2>/dev/null) || \
		{ echo "错误: 未找到 FFmpeg (尝试: brew install ffmpeg)"; exit 1; }
	@echo "✓ FFmpeg 检查通过"

.PHONY: rebuild-deps
rebuild-deps:
	@echo "重新构建所有子模块..."
	@$(MAKE) -C event_base clean && $(MAKE) -C event_base all
	@$(MAKE) -C network clean && $(MAKE) -C network all
	@$(MAKE) -C pack clean && $(MAKE) -C pack all
	@$(MAKE) -C av_codec clean && $(MAKE) -C av_codec all
	@echo "✓ 子模块重新构建完成"

# ============================================================================
# 清理
# ============================================================================

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
	@$(MAKE) -C av_codec clean 2>/dev/null || true
	@echo "✓ 深度清理完成"

# ============================================================================
# 运行
# ============================================================================

.PHONY: run-sender run-receiver
run-sender: $(SENDER_EXE)
	$(SENDER_EXE) 127.0.0.1 9000

run-receiver: $(RECEIVER_EXE)
	$(RECEIVER_EXE) 9000

.PHONY: run-av-sender run-av-receiver
run-av-sender: av
	@echo "用法: $(AV_SENDER_EXE) <媒体文件> <服务器地址> <端口>"
	@echo "示例: $(AV_SENDER_EXE) video.mp4 127.0.0.1 9000"

run-av-receiver: av
	@echo "用法: $(AV_RECEIVER_EXE) <端口> <输出文件>"
	@echo "示例: $(AV_RECEIVER_EXE) 9000 output.mp4"

# ============================================================================
# 测试说明
# ============================================================================

.PHONY: test
test: all
	@echo ""
	@echo "=========================================="
	@echo "基础版本测试："
	@echo "  终端1: make run-receiver"
	@echo "  终端2: make run-sender"
	@echo "=========================================="

.PHONY: test-av
test-av: av
	@echo ""
	@echo "=========================================="
	@echo "音视频版本测试（带FEC）："
	@echo "  终端1: $(AV_RECEIVER_EXE) 9000 output.mp4"
	@echo "  终端2: $(AV_SENDER_EXE) input.mp4 127.0.0.1 9000"
	@echo "=========================================="

.PHONY: test-av-base
test-av-base: av-base
	@echo ""
	@echo "=========================================="
	@echo "基础音视频版本测试（无FEC）："
	@echo "  终端1: av_codec/build/av_receiver_demo 9000 output.mp4"
	@echo "  终端2: av_codec/build/av_sender_demo input.mp4 127.0.0.1 9000"
	@echo "=========================================="

# ============================================================================
# 帮助
# ============================================================================

.PHONY: help
help:
	@echo "RaptorQStream 编译系统"
	@echo ""
	@echo "模块结构:"
	@echo "  av_codec/   - 音视频编解码 + 基础网络传输"
	@echo "  pack/       - RaptorQ FEC 编码"
	@echo "  network/    - UDP 网络传输"
	@echo "  event_base/ - 事件循环"
	@echo ""
	@echo "编译目标:"
	@echo "  make all          - 编译基础版本"
	@echo "  make av           - 编译带FEC的音视频版本"
	@echo "  make av-base      - 编译基础音视频版本(无FEC)"
	@echo "  make all-versions - 编译所有版本"
	@echo ""
	@echo "运行:"
	@echo "  make run-sender     - 运行基础发送端"
	@echo "  make run-receiver   - 运行基础接收端"
	@echo "  make run-av-sender  - 显示音视频发送端用法"
	@echo "  make run-av-receiver - 显示音视频接收端用法"
	@echo ""
	@echo "测试说明:"
	@echo "  make test         - 基础版本测试"
	@echo "  make test-av      - 音视频+FEC版本测试"
	@echo "  make test-av-base - 基础音视频版本测试"
	@echo ""
	@echo "其他:"
	@echo "  make clean        - 清理构建"
	@echo "  make distclean    - 深度清理"
	@echo "  make rebuild-deps - 重建所有子模块"
	@echo "  make help         - 显示此帮助"

# ============================================================================
# 单独编译目标
# ============================================================================

.PHONY: sender receiver av-sender av-receiver
sender: check-deps $(BUILD_DIR) $(SENDER_EXE)
receiver: check-deps $(BUILD_DIR) $(RECEIVER_EXE)
av-sender: check-deps check-ffmpeg $(BUILD_DIR) build-av-codec $(AV_SENDER_EXE)
av-receiver: check-deps check-ffmpeg $(BUILD_DIR) build-av-codec $(AV_RECEIVER_EXE)
