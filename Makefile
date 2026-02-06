# Makefile for Sender and Receiver Demo

# 编译器和标志
CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC
INCLUDES = -I. -I./pack -I./network -I./event_base -I./libRaptorQ/src
LDFLAGS = -L./libRaptorQ/build/lib

# 库
LIBS = -lRaptorQ -lpthread

# 自动检测 libevent 路径
LIBEVENT_CFLAGS := $(shell pkg-config --cflags libevent 2>/dev/null || echo "-I/usr/include")
LIBEVENT_LIBS := $(shell pkg-config --libs libevent 2>/dev/null || echo "-levent -levent_core -levent_pthreads")

INCLUDES += $(LIBEVENT_CFLAGS)
LDFLAGS  += $(shell pkg-config --libs --libs-only-L libevent 2>/dev/null || echo "-L/usr/lib")
LIBS    += $(LIBEVENT_LIBS)

# 构建目录
BUILD_DIR = build

# 源文件
SENDER_SRC = sender_demo.cpp sender.cpp send_center.cpp
RECEIVER_SRC = receiver_demo.cpp receiver.cpp receiver_center.cpp

NETWORK_OBJS = network/build/network_server.o network/build/network_client.o
EVENT_OBJS = event_base/build/event_loop.o
PACK_LIB = pack/build/librqpack.a

# 目标文件
SENDER_OBJS = $(addprefix $(BUILD_DIR)/, $(SENDER_SRC:.cpp=.o))
RECEIVER_OBJS = $(addprefix $(BUILD_DIR)/, $(RECEIVER_SRC:.cpp=.o))

# 可执行文件
SENDER_EXE = $(BUILD_DIR)/sender_demo
RECEIVER_EXE = $(BUILD_DIR)/receiver_demo

# 默认目标
.PHONY: all
all: check-deps $(BUILD_DIR) $(SENDER_EXE) $(RECEIVER_EXE)
	@echo ""
	@echo "=========================================="
	@echo "编译完成！"
	@echo "发送端: $(SENDER_EXE)"
	@echo "接收端: $(RECEIVER_EXE)"
	@echo "=========================================="

# 创建构建目录
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# 编译 sender_demo
$(SENDER_EXE): $(SENDER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接发送端..."
	$(CXX) $(CXXFLAGS) -o $@ $(SENDER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB) $(LDFLAGS) $(LIBS)
	@echo "✓ 发送端编译完成: $@"

# 编译 receiver_demo
$(RECEIVER_EXE): $(RECEIVER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB)
	@echo "链接接收端..."
	$(CXX) $(CXXFLAGS) -o $@ $(RECEIVER_OBJS) $(NETWORK_OBJS) $(EVENT_OBJS) $(PACK_LIB) $(LDFLAGS) $(LIBS)
	@echo "✓ 接收端编译完成: $@"

# 编译 sender 相关对象文件
$(BUILD_DIR)/sender.o: sender.cpp sender.h common.h
	@echo "编译 sender.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/send_center.o: send_center.cpp send_center.h sender.h
	@echo "编译 send_center.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/sender_demo.o: sender_demo.cpp send_center.h
	@echo "编译 sender_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 编译 receiver 相关对象文件
$(BUILD_DIR)/receiver.o: receiver.cpp receiver.h common.h
	@echo "编译 receiver.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/receiver_center.o: receiver_center.cpp receiver_center.h receiver.h
	@echo "编译 receiver_center.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/receiver_demo.o: receiver_demo.cpp receiver_center.h
	@echo "编译 receiver_demo.cpp..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 检查依赖并构建子模块
.PHONY: check-deps
check-deps:
	@echo "检查依赖..."
	@command -v $(CXX) >/dev/null 2>&1 || { echo "错误: 未找到 g++"; exit 1; }
	@pkg-config --exists libevent || { echo "错误: 未找到 libevent (请安装: sudo apt install libevent-dev)"; exit 1; }
	@echo "✓ 依赖检查通过"
	@echo ""
	@echo "构建子模块..."
	@$(MAKE) -C event_base all
	@$(MAKE) -C network all
	@$(MAKE) -C pack all

# 强制重新构建子模块
.PHONY: rebuild-deps
rebuild-deps:
	@echo "重新构建所有子模块..."
	@$(MAKE) -C event_base clean && $(MAKE) -C event_base all
	@$(MAKE) -C network clean && $(MAKE) -C network all
	@$(MAKE) -C pack clean && $(MAKE) -C pack all
	@echo "✓ 子模块重新构建完成"

# 清理
.PHONY: clean
clean:
	@echo "清理构建文件..."
	rm -rf $(BUILD_DIR)
	@echo "✓ 清理完成"

# 深度清理（包括子模块）
.PHONY: distclean
distclean: clean
	@echo "清理所有子模块..."
	@$(MAKE) -C pack clean
	@$(MAKE) -C network clean
	@$(MAKE) -C event_base clean
	@echo "✓ 深度清理完成"

# 运行发送端
.PHONY: run-sender
run-sender: $(SENDER_EXE)
	@echo "运行发送端..."
	$(SENDER_EXE) 127.0.0.1 9000

# 运行接收端
.PHONY: run-receiver
run-receiver: $(RECEIVER_EXE)
	@echo "运行接收端..."
	$(RECEIVER_EXE) 9000

# 测试（在两个终端中运行）
.PHONY: test
test: all
	@echo ""
	@echo "=========================================="
	@echo "测试说明："
	@echo "1. 在终端1中运行: make run-receiver"
	@echo "2. 在终端2中运行: make run-sender"
	@echo "=========================================="

# 帮助
.PHONY: help
help:
	@echo "可用目标："
	@echo "  make all            - 编译所有程序（默认）"
	@echo "  make sender         - 仅编译发送端"
	@echo "  make receiver       - 仅编译接收端"
	@echo "  make rebuild-deps   - 重新构建所有依赖"
	@echo "  make clean          - 清理构建文件"
	@echo "  make distclean      - 深度清理（包括子模块）"
	@echo "  make run-sender     - 运行发送端"
	@echo "  make run-receiver   - 运行接收端"
	@echo "  make test           - 显示测试说明"
	@echo "  make help           - 显示此帮助"

# 单独编译目标
.PHONY: sender receiver
sender: check-deps $(BUILD_DIR) $(SENDER_EXE)
receiver: check-deps $(BUILD_DIR) $(RECEIVER_EXE)
