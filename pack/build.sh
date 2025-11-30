#!/bin/bash

# RQPack 构建脚本

set -e

echo "======================================"
echo "  RQPack 构建脚本"
echo "======================================"

# 检查 libRaptorQ 是否已编译
LIBRAPTORQ_LIB="../libRaptorQ/build/lib/libRaptorQ.dylib"
if [ ! -f "$LIBRAPTORQ_LIB" ] && [ ! -f "../libRaptorQ/build/lib/libRaptorQ.so" ]; then
    echo ""
    echo "错误: 找不到 libRaptorQ 库文件"
    echo "请先编译 libRaptorQ:"
    echo ""
    echo "  cd ../libRaptorQ/build"
    echo "  cmake -DCMAKE_BUILD_TYPE=Release .."
    echo "  make -j4"
    echo ""
    exit 1
fi

echo ""
echo "✓ 找到 libRaptorQ 库"

# 清理旧文件（可选）
if [ "$1" == "clean" ]; then
    echo ""
    echo "--- 清理旧文件 ---"
    make clean
    exit 0
fi

# 编译
echo ""
echo "--- 编译 ---"
make -j4

# 检查编译结果
if [ -f "build/test_pack" ] && [ -f "build/example" ]; then
    echo ""
    echo "可以运行以下程序:"
    echo "  build/example      # 简单示例"
    echo "  build/test_pack    # 完整测试"
    echo ""
    echo "或使用 make 命令:"
    echo "  make run-example  # 运行示例"
    echo "  make test         # 运行测试"
    echo ""
else
    echo ""
    echo "✗ 编译失败"
    exit 1
fi

