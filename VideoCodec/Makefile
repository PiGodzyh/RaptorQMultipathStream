# VideoCodec Makefile (H.264 NAL Passthrough)
#
# 功能: 从MP4读取H.264 NAL单元，不解码为YUV；支持写入H.264到MP4
#
# 主要组件:
#   - VideoReader: 读取H.264编码数据，识别I/P/B帧
#   - VideoWriter: 写入H.264数据到MP4容器

CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC

# FFmpeg 库
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavcodec libavformat libavutil 2>/dev/null || echo "")
FFMPEG_LIBS := $(shell pkg-config --libs libavcodec libavformat libavutil 2>/dev/null || echo "-lavcodec -lavformat -lavutil")

CXXFLAGS += $(FFMPEG_CFLAGS)
LIBS = $(FFMPEG_LIBS) -lpthread

# 目录
BUILD_DIR = build
DATA_DIR = ../data/videos
OUTPUT_DIR = ../output/videos

# 源文件
LIB_SRC = video_reader.cpp video_writer.cpp
TEST_SRC = test_codec.cpp

# 目标文件
LIB_OBJS = $(addprefix $(BUILD_DIR)/, $(LIB_SRC:.cpp=.o))
TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(TEST_SRC:.cpp=.o))

# 静态库
STATIC_LIB = $(BUILD_DIR)/libvideocodec.a

# 可执行文件
TEST_CODEC = $(BUILD_DIR)/test_codec

# 默认目标
.PHONY: all
all: dirs $(STATIC_LIB) $(TEST_CODEC)
	@echo ""
	@echo "=========================================="
	@echo "编译完成！(H.264 NAL透传模式)"
	@echo "静态库: $(STATIC_LIB)"
	@echo "测试程序: $(TEST_CODEC)"
	@echo "=========================================="

# 创建目录
.PHONY: dirs
dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(OUTPUT_DIR)

# 编译静态库
$(STATIC_LIB): $(LIB_OBJS)
	@echo "创建静态库 $@ ..."
	ar rcs $@ $^
	@echo "✓ 静态库创建完成"

# 编译 VideoReader
$(BUILD_DIR)/video_reader.o: video_reader.cpp video_reader.h video_codec.h
	@echo "编译 $< ..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译 VideoWriter
$(BUILD_DIR)/video_writer.o: video_writer.cpp video_writer.h video_codec.h
	@echo "编译 $< ..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译测试程序
$(BUILD_DIR)/test_codec.o: test_codec.cpp video_reader.h video_writer.h video_codec.h
	@echo "编译 $< ..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_CODEC): $(TEST_OBJS) $(STATIC_LIB)
	@echo "链接 $@ ..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "✓ 测试程序编译完成"

# 测试
.PHONY: test
test: $(TEST_CODEC)
	@echo "=========================================="
	@echo "测试 H.264 NAL透传"
	@echo "=========================================="
	@if [ -f "$(DATA_DIR)/test.mp4" ]; then \
		echo "1. 测试读取: $(DATA_DIR)/test.mp4"; \
		$(TEST_CODEC) "$(DATA_DIR)/test.mp4"; \
		echo ""; \
		echo "2. 测试读取+写入..."; \
		$(TEST_CODEC) "$(DATA_DIR)/test.mp4" "$(OUTPUT_DIR)/test_output.mp4"; \
		echo ""; \
		echo "3. 验证输出文件..."; \
		$(TEST_CODEC) "$(OUTPUT_DIR)/test_output.mp4"; \
	else \
		echo "提示: 未找到 $(DATA_DIR)/test.mp4"; \
		echo "请将要测试的视频文件放入 $(DATA_DIR)/ 目录"; \
		echo "或者指定其他路径: $(TEST_CODEC) <输入> [输出]"; \
	fi

# 清理
.PHONY: clean
clean:
	@echo "清理构建文件..."
	rm -rf $(BUILD_DIR)
	@echo "✓ 清理完成"

# 帮助
.PHONY: help
help:
	@echo "VideoCodec Makefile (H.264 NAL透传)"
	@echo ""
	@echo "可用目标:"
	@echo "  make all       - 编译库和测试程序 (默认)"
	@echo "  make test      - 运行测试"
	@echo "  make clean     - 清理构建文件"
	@echo "  make help      - 显示此帮助"
	@echo ""
	@echo "使用说明:"
	@echo "  1. 将要测试的视频文件放入 $(DATA_DIR)/"
	@echo "  2. 运行 make test 测试H.264透传功能"
	@echo ""
	@echo "API说明:"
	@echo "  VideoReader - 从MP4读取H.264 NAL单元"
	@echo "  VideoWriter - 将H.264 NAL单元写入MP4"
