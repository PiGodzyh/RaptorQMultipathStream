# RQPack Makefile

# 编译器设置
CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -fPIC

# 目录设置
BUILD_DIR = build
LIBRAPTORQ_DIR = ../libRaptorQ
LIBRAPTORQ_BUILD = $(LIBRAPTORQ_DIR)/build
LIBRAPTORQ_SRC = $(LIBRAPTORQ_DIR)/src

# 包含路径
INCLUDES = -I. -I$(LIBRAPTORQ_SRC)

# 库路径和链接库
LDFLAGS = -L$(LIBRAPTORQ_BUILD)/lib
LIBS = -lRaptorQ -lpthread

# 目标文件
LIB_TARGET = $(BUILD_DIR)/librqpack.a
EXAMPLE_TARGET = $(BUILD_DIR)/example
TEST_TARGET = $(BUILD_DIR)/test_pack

# 源文件和目标文件
LIB_SRC = rq_pack.cpp
LIB_OBJ = $(BUILD_DIR)/rq_pack.o

EXAMPLE_SRC = example.cpp
EXAMPLE_OBJ = $(BUILD_DIR)/example.o

TEST_SRC = test_pack.cpp
TEST_OBJ = $(BUILD_DIR)/test_pack.o

# 默认目标
.PHONY: all clean help test run-example

all: check-deps $(BUILD_DIR) $(LIB_TARGET) $(EXAMPLE_TARGET) $(TEST_TARGET)
	@echo ""
	@echo "======================================"
	@echo "  ✓ 编译完成！"
	@echo "======================================"
	@echo ""
	@echo "生成的文件:"
	@echo "  $(LIB_TARGET)  - 静态库"
	@echo "  $(EXAMPLE_TARGET)  - 简单示例程序"
	@echo "  $(TEST_TARGET)  - 测试程序"
	@echo ""
	@echo "运行示例:"
	@echo "  $(EXAMPLE_TARGET)"
	@echo "  $(TEST_TARGET)"
	@echo ""
	@echo "或使用 make 命令:"
	@echo "  make run-example"
	@echo "  make test"
	@echo ""

# 创建 build 目录
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# 编译静态库
$(LIB_TARGET): $(LIB_OBJ)
	@echo "创建静态库: $@"
	ar rcs $@ $^

# 编译示例程序
$(EXAMPLE_TARGET): $(EXAMPLE_OBJ) $(LIB_TARGET)
	@echo "链接示例程序: $@"
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB_TARGET) $(LDFLAGS) $(LIBS)

# 编译测试程序
$(TEST_TARGET): $(TEST_OBJ) $(LIB_TARGET)
	@echo "链接测试程序: $@"
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB_TARGET) $(LDFLAGS) $(LIBS)

# 编译 .cpp 到 .o
$(BUILD_DIR)/%.o: %.cpp rq_pack.h | $(BUILD_DIR)
	@echo "编译: $<"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 运行示例
run-example: $(EXAMPLE_TARGET)
	@echo "运行示例程序..."
	@echo ""
	@$(EXAMPLE_TARGET)

# 运行测试
test: $(TEST_TARGET)
	@echo "运行测试..."
	@echo ""
	@$(TEST_TARGET)

# 清理
clean:
	@echo "清理生成文件..."
	@rm -rf $(BUILD_DIR)
	@echo "清理完成"

# 帮助信息
help:
	@echo "RQPack Makefile 使用说明"
	@echo ""
	@echo "可用目标:"
	@echo "  make              - 编译所有目标（默认）"
	@echo "  make all          - 编译所有目标"
	@echo "  make clean        - 清理生成的文件"
	@echo "  make test         - 编译并运行测试"
	@echo "  make run-example  - 编译并运行示例"
	@echo "  make help         - 显示此帮助信息"
	@echo ""
	@echo "使用前请确保 libRaptorQ 已编译:"
	@echo "  cd $(LIBRAPTORQ_DIR)/build"
	@echo "  cmake -DCMAKE_BUILD_TYPE=Release .."
	@echo "  make -j4"
	@echo ""

# 检查依赖
.PHONY: check-deps
check-deps:
	@if [ ! -f "$(LIBRAPTORQ_BUILD)/lib/libRaptorQ.dylib" ] && \
	    [ ! -f "$(LIBRAPTORQ_BUILD)/lib/libRaptorQ.so" ] && \
	    [ ! -f "$(LIBRAPTORQ_BUILD)/lib/libRaptorQ.a" ]; then \
		echo "错误: 找不到 libRaptorQ 库文件"; \
		echo "请先编译 libRaptorQ:"; \
		echo "  cd $(LIBRAPTORQ_DIR)/build"; \
		echo "  cmake -DCMAKE_BUILD_TYPE=Release .."; \
		echo "  make -j4"; \
		exit 1; \
	fi
	@echo "✓ 找到 libRaptorQ 库"

