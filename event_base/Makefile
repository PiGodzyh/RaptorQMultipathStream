# Makefile for event_base module

CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC

# 自动检测 libevent 路径
LIBEVENT_PREFIX := $(shell brew --prefix libevent 2>/dev/null || echo "/usr/local")
INCLUDES = -I. -I$(LIBEVENT_PREFIX)/include

BUILD_DIR = build

# 源文件
SRCS = event_loop.cpp
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.cpp=.o))

.PHONY: all
all: $(BUILD_DIR) $(OBJS)
	@echo "✓ event_base 模块编译完成"

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.cpp %.h
	@echo "编译 $<..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	@echo "✓ event_base 清理完成"

