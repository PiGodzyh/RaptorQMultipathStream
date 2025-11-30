# Network 模块 Makefile

# 编译器设置
CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC

# 目录设置
BUILD_DIR = build

# 检测 libevent 路径（支持 brew 安装）
LIBEVENT_PREFIX := $(shell brew --prefix libevent 2>/dev/null || echo "/usr/local")

# 包含路径
INCLUDES = -I. -I$(LIBEVENT_PREFIX)/include

# 库路径和链接库
LDFLAGS = -L$(LIBEVENT_PREFIX)/lib
LIBS = -levent -lpthread

# 目标文件
LIB_TARGET = $(BUILD_DIR)/libnetwork.a
SERVER_EXAMPLE = $(BUILD_DIR)/server_example
CLIENT_EXAMPLE = $(BUILD_DIR)/client_example

# 源文件和目标文件
LIB_SOURCES = network_server.cpp network_client.cpp
LIB_OBJECTS = $(BUILD_DIR)/network_server.o $(BUILD_DIR)/network_client.o

SERVER_EXAMPLE_OBJ = $(BUILD_DIR)/server_example.o
CLIENT_EXAMPLE_OBJ = $(BUILD_DIR)/client_example.o

# 默认目标
.PHONY: all clean help examples

all: check-deps $(BUILD_DIR) $(LIB_TARGET) $(SERVER_EXAMPLE) $(CLIENT_EXAMPLE)
	@echo ""
	@echo "======================================"
	@echo "  ✓ 编译完成！"
	@echo "======================================"
	@echo ""
	@echo "生成的文件:"
	@echo "  $(LIB_TARGET)    - 静态库"
	@echo "  $(SERVER_EXAMPLE)  - 服务器示例"
	@echo "  $(CLIENT_EXAMPLE)  - 客户端示例"
	@echo ""
	@echo "运行示例:"
	@echo "  $(SERVER_EXAMPLE) [端口]"
	@echo "  $(CLIENT_EXAMPLE) [服务器地址] [端口]"
	@echo ""
	@echo "或使用 make 命令:"
	@echo "  make run-server"
	@echo "  make run-client"
	@echo ""

# 创建 build 目录
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# 编译静态库
$(LIB_TARGET): $(LIB_OBJECTS)
	@echo "创建静态库: $@"
	ar rcs $@ $^

# 编译服务器示例
$(SERVER_EXAMPLE): $(SERVER_EXAMPLE_OBJ) $(LIB_TARGET)
	@echo "链接服务器示例: $@"
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB_TARGET) $(LDFLAGS) $(LIBS)

# 编译客户端示例
$(CLIENT_EXAMPLE): $(CLIENT_EXAMPLE_OBJ) $(LIB_TARGET)
	@echo "链接客户端示例: $@"
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB_TARGET) $(LDFLAGS) $(LIBS)

# 编译 .cpp 到 .o
$(BUILD_DIR)/%.o: %.cpp *.h | $(BUILD_DIR)
	@echo "编译: $<"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 运行服务器示例
run-server: $(SERVER_EXAMPLE)
	@echo "运行服务器示例（端口 8888）..."
	@echo ""
	@$(SERVER_EXAMPLE) 8888

# 运行客户端示例
run-client: $(CLIENT_EXAMPLE)
	@echo "运行客户端示例..."
	@echo ""
	@$(CLIENT_EXAMPLE) 127.0.0.1 8888

# 清理
clean:
	@echo "清理生成文件..."
	@rm -rf $(BUILD_DIR)
	@echo "清理完成"

# 帮助信息
help:
	@echo "Network 模块 Makefile 使用说明"
	@echo ""
	@echo "可用目标:"
	@echo "  make              - 编译所有目标（默认）"
	@echo "  make all          - 编译所有目标"
	@echo "  make clean        - 清理生成的文件"
	@echo "  make run-server   - 运行服务器示例"
	@echo "  make run-client   - 运行客户端示例"
	@echo "  make help         - 显示此帮助信息"
	@echo ""
	@echo "运行示例:"
	@echo "  $(SERVER_EXAMPLE) [端口]"
	@echo "  $(CLIENT_EXAMPLE) [服务器地址] [端口]"
	@echo ""
	@echo "注意: 需要安装 libevent 库"
	@echo "  macOS: brew install libevent"
	@echo "  Ubuntu: sudo apt-get install libevent-dev"
	@echo ""

# 检查依赖
.PHONY: check-deps
check-deps:
	@echo "✓ 检查依赖"
	@command -v $(CXX) >/dev/null 2>&1 || \
		(echo "错误: 找不到 g++ 编译器" && exit 1)
	@if [ ! -d "$(LIBEVENT_PREFIX)/include/event2" ]; then \
		echo "错误: 找不到 libevent 头文件"; \
		echo "请安装 libevent: brew install libevent"; \
		exit 1; \
	fi
	@echo "✓ libevent 路径: $(LIBEVENT_PREFIX)"

