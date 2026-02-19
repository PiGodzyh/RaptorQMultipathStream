# VideoCodec Makefile

CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC

# FFmpeg 库
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavcodec libavformat libavutil libswscale 2>/dev/null || echo "")
FFMPEG_LIBS := $(shell pkg-config --libs libavcodec libavformat libavutil libswscale 2>/dev/null || echo "-lavcodec -lavformat -lavutil -lswscale")

CXXFLAGS += $(FFMPEG_CFLAGS)
LIBS = $(FFMPEG_LIBS) -lpthread

# 目录
BUILD_DIR = build
DATA_DIR = ../data/videos
OUTPUT_DIR = ../output/videos

# 源文件
LIB_SRC = video_encoder.cpp video_decoder.cpp
DEMO_SRC = encode_demo.cpp decode_demo.cpp transcode_demo.cpp

# 目标文件
LIB_OBJS = $(addprefix $(BUILD_DIR)/, $(LIB_SRC:.cpp=.o))
DEMO_OBJS = $(addprefix $(BUILD_DIR)/, $(DEMO_SRC:.cpp=.o))

# 静态库
STATIC_LIB = $(BUILD_DIR)/libvideocodec.a

# 可执行文件
ENCODE_DEMO = $(BUILD_DIR)/encode_demo
DECODE_DEMO = $(BUILD_DIR)/decode_demo
TRANSCODE_DEMO = $(BUILD_DIR)/transcode_demo

# 默认目标
.PHONY: all
all: dirs $(STATIC_LIB) $(ENCODE_DEMO) $(DECODE_DEMO) $(TRANSCODE_DEMO)
	@echo ""
	@echo "=========================================="
	@echo "编译完成！"
	@echo "静态库: $(STATIC_LIB)"
	@echo "编码测试: $(ENCODE_DEMO)"
	@echo "解码测试: $(DECODE_DEMO)"
	@echo "转码测试: $(TRANSCODE_DEMO)"
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

# 编译编码器对象
$(BUILD_DIR)/video_encoder.o: video_encoder.cpp video_encoder.h video_codec.h
	@echo "编译 $< ..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/video_decoder.o: video_decoder.cpp video_decoder.h video_codec.h
	@echo "编译 $< ..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译 encode_demo
$(BUILD_DIR)/encode_demo.o: encode_demo.cpp video_encoder.h video_codec.h
	@echo "编译 $< ..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ENCODE_DEMO): $(BUILD_DIR)/encode_demo.o $(STATIC_LIB)
	@echo "链接 $@ ..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "✓ 编码测试程序编译完成"

# 编译 decode_demo
$(BUILD_DIR)/decode_demo.o: decode_demo.cpp video_decoder.h video_codec.h
	@echo "编译 $< ..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DECODE_DEMO): $(BUILD_DIR)/decode_demo.o $(STATIC_LIB)
	@echo "链接 $@ ..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "✓ 解码测试程序编译完成"

# 编译 transcode_demo
$(BUILD_DIR)/transcode_demo.o: transcode_demo.cpp video_encoder.h video_decoder.h video_codec.h
	@echo "编译 $< ..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TRANSCODE_DEMO): $(BUILD_DIR)/transcode_demo.o $(STATIC_LIB)
	@echo "链接 $@ ..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)
	@echo "✓ 转码测试程序编译完成"

# 测试编码
.PHONY: test-encode
test-encode: $(ENCODE_DEMO)
	@echo "=========================================="
	@echo "测试编码 (读取视频文件)"
	@echo "=========================================="
	@if [ -f "$(DATA_DIR)/test.mp4" ]; then \
		$(ENCODE_DEMO) "$(DATA_DIR)/test.mp4"; \
	else \
		echo "提示: 未找到 $(DATA_DIR)/test.mp4"; \
		echo "请将要测试的视频文件放入 $(DATA_DIR)/ 目录"; \
		echo "或者指定其他路径: $(ENCODE_DEMO) <视频路径>"; \
	fi

# 测试解码
.PHONY: test-decode
test-decode: $(DECODE_DEMO)
	@echo "=========================================="
	@echo "测试解码 (生成测试视频)"
	@echo "=========================================="
	$(DECODE_DEMO) "$(OUTPUT_DIR)/test_output.mp4"

# 测试转码
.PHONY: test-transcode
test-transcode: $(TRANSCODE_DEMO)
	@echo "=========================================="
	@echo "测试转码 (读取并重新编码)"
	@echo "=========================================="
	@if [ -f "$(DATA_DIR)/test.mp4" ]; then \
		$(TRANSCODE_DEMO) "$(DATA_DIR)/test.mp4" "$(OUTPUT_DIR)/transcoded.mp4"; \
	else \
		echo "提示: 未找到 $(DATA_DIR)/test.mp4"; \
		echo "使用生成的测试视频进行转码..."; \
		$(DECODE_DEMO) "$(OUTPUT_DIR)/temp_source.mp4" -n 100; \
		$(TRANSCODE_DEMO) "$(OUTPUT_DIR)/temp_source.mp4" "$(OUTPUT_DIR)/transcoded.mp4"; \
	fi

# 运行所有测试
.PHONY: test
test: test-encode test-decode test-transcode

# 清理
.PHONY: clean
clean:
	@echo "清理构建文件..."
	rm -rf $(BUILD_DIR)
	@echo "✓ 清理完成"

# 帮助
.PHONY: help
help:
	@echo "可用目标："
	@echo "  make all            - 编译所有程序（默认）"
	@echo "  make test-encode    - 运行编码测试（读取视频）"
	@echo "  make test-decode    - 运行解码测试（生成视频）"
	@echo "  make test-transcode - 运行转码测试（读取+重新编码）"
	@echo "  make test           - 运行所有测试"
	@echo "  make clean          - 清理构建文件"
	@echo "  make help           - 显示此帮助"
	@echo ""
	@echo "使用说明："
	@echo "  1. 将要测试的视频文件放入 $(DATA_DIR)/"
	@echo "  2. 运行 make test-encode 测试视频读取"
	@echo "  3. 运行 make test-decode 测试视频生成"
	@echo "  4. 运行 make test-transcode 测试端到端转码"
