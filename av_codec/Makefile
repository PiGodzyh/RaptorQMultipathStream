# Makefile for av_codec module
# 音视频编解码 + 网络传输

CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC
BUILD_DIR = build

# FFmpeg 配置
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavcodec libavformat libswscale libswresample libavutil 2>/dev/null)
FFMPEG_LIBS := $(shell pkg-config --libs libavcodec libavformat libswscale libswresample libavutil 2>/dev/null)

ifeq ($(FFMPEG_LIBS),)
    FFMPEG_PREFIX := $(shell brew --prefix ffmpeg 2>/dev/null || echo "/usr/local")
    FFMPEG_CFLAGS = -I$(FFMPEG_PREFIX)/include
    FFMPEG_LIBS = -L$(FFMPEG_PREFIX)/lib -lavcodec -lavformat -lswscale -lswresample -lavutil
endif

# libevent 配置
LIBEVENT_PREFIX := $(shell brew --prefix libevent 2>/dev/null || echo "/usr/local")

# 包含路径
INCLUDES = -I. -I.. -I../network $(FFMPEG_CFLAGS) -I$(LIBEVENT_PREFIX)/include

# 链接标志
LDFLAGS = -L$(LIBEVENT_PREFIX)/lib

# 库
LIBS = -lpthread -levent -levent_core $(FFMPEG_LIBS)

# network 模块对象
NETWORK_OBJS = ../network/build/network_server.o ../network/build/network_client.o

# 源文件
LIB_SRCS = av_codec.cpp av_sender.cpp av_receiver.cpp
LIB_OBJS = $(addprefix $(BUILD_DIR)/, $(LIB_SRCS:.cpp=.o))
LIB = $(BUILD_DIR)/libavcodec_module.a

# Demo
SENDER_DEMO = $(BUILD_DIR)/av_sender_demo
RECEIVER_DEMO = $(BUILD_DIR)/av_receiver_demo

.PHONY: all clean check-deps demo lib

# 默认编译库
all: check-deps build-network $(BUILD_DIR) lib
	@echo "✓ av_codec 模块编译完成"

# 只编译库
lib: $(LIB)

# 编译 demo
demo: check-deps build-network $(BUILD_DIR) $(LIB) $(SENDER_DEMO) $(RECEIVER_DEMO)
	@echo ""
	@echo "=========================================="
	@echo "av_codec Demo 编译完成（基础版本，无FEC）"
	@echo "  发送端: $(SENDER_DEMO)"
	@echo "  接收端: $(RECEIVER_DEMO)"
	@echo "=========================================="

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# 编译静态库
$(LIB): $(LIB_OBJS)
	@echo "创建静态库..."
	ar rcs $@ $^

# 编译对象文件
$(BUILD_DIR)/av_codec.o: av_codec.cpp av_codec.h
	@echo "编译 av_codec.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/av_sender.o: av_sender.cpp av_sender.h av_codec.h
	@echo "编译 av_sender.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/av_receiver.o: av_receiver.cpp av_receiver.h av_codec.h
	@echo "编译 av_receiver.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/av_sender_demo.o: av_sender_demo.cpp av_sender.h
	@echo "编译 av_sender_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/av_receiver_demo.o: av_receiver_demo.cpp av_receiver.h
	@echo "编译 av_receiver_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 链接 demo
$(SENDER_DEMO): $(BUILD_DIR)/av_sender_demo.o $(LIB) $(NETWORK_OBJS)
	@echo "链接发送端..."
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB) $(NETWORK_OBJS) $(LDFLAGS) $(LIBS)
	@echo "✓ 发送端编译完成: $@"

$(RECEIVER_DEMO): $(BUILD_DIR)/av_receiver_demo.o $(LIB) $(NETWORK_OBJS)
	@echo "链接接收端..."
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB) $(NETWORK_OBJS) $(LDFLAGS) $(LIBS)
	@echo "✓ 接收端编译完成: $@"

# 构建 network 模块
.PHONY: build-network
build-network:
	@echo "构建 network 模块..."
	@$(MAKE) -C ../network all

check-deps:
	@echo "检查 FFmpeg 依赖..."
	@pkg-config --exists libavcodec 2>/dev/null || \
		(test -d "$$(brew --prefix ffmpeg 2>/dev/null)/include" 2>/dev/null) || \
		{ echo "错误: 未找到 FFmpeg (尝试: brew install ffmpeg)"; exit 1; }
	@echo "✓ FFmpeg 检查通过"

clean:
	@echo "清理 av_codec 构建文件..."
	rm -rf $(BUILD_DIR)
	@echo "✓ 清理完成"

# 运行
.PHONY: run-sender run-receiver
run-sender: demo
	@echo "用法: $(SENDER_DEMO) <媒体文件> <服务器地址> <端口>"
	@echo "示例: $(SENDER_DEMO) video.mp4 127.0.0.1 9000"

run-receiver: demo
	@echo "用法: $(RECEIVER_DEMO) <端口> <输出文件>"
	@echo "示例: $(RECEIVER_DEMO) 9000 output.mp4"

# 测试说明
.PHONY: test
test: demo
	@echo ""
	@echo "=========================================="
	@echo "基础音视频传输测试（无FEC）："
	@echo "  终端1: $(RECEIVER_DEMO) 9000 output.mp4"
	@echo "  终端2: $(SENDER_DEMO) input.mp4 127.0.0.1 9000"
	@echo "=========================================="

# 帮助
.PHONY: help
help:
	@echo "av_codec 模块编译系统"
	@echo ""
	@echo "目标:"
	@echo "  make all       - 编译 av_codec 库"
	@echo "  make demo      - 编译 demo (基础音视频传输)"
	@echo "  make clean     - 清理构建文件"
	@echo "  make test      - 显示测试说明"
	@echo "  make help      - 显示此帮助"

# 显示配置
.PHONY: info
info:
	@echo "FFmpeg CFLAGS: $(FFMPEG_CFLAGS)"
	@echo "FFmpeg LIBS: $(FFMPEG_LIBS)"
	@echo "INCLUDES: $(INCLUDES)"
