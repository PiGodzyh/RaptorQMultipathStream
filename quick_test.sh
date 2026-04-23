#!/bin/bash
# quick_test.sh - 快速验证所有数据类型

cd "$(dirname "$0")"
mkdir -p output

echo "=========================================="
echo "  UnifiedSender 快速测试"
echo "=========================================="

# 清理
pkill -9 -f "multi_streaming_demo\|voice_demo" 2>/dev/null || true
sleep 1

# 1. FC Control Test
echo ""
echo "[1/4] FC Control 测试..."
./build/multi_streaming_demo receiver fc 9000 output/fc.log &
sleep 1
echo -e "TAKEOFF\nMOVE 1 2 3\nLAND\nquit" | timeout 3 ./build/multi_streaming_demo sender fc 127.0.0.1 9000 2>/dev/null
sleep 1
pkill -f "multi_streaming_demo receiver fc" 2>/dev/null || true
[ -f "output/fc.log" ] && echo "✓ FC Control 通过" || echo "✗ FC Control 失败"

# 2. Point Cloud Test
echo ""
echo "[2/4] Point Cloud 测试..."
./build/multi_streaming_demo receiver pointcloud 9002 output/test.pcd &
sleep 1
timeout 5 ./build/multi_streaming_demo sender pointcloud 127.0.0.1 9002 "30:2" 2>/dev/null
sleep 1
pkill -f "multi_streaming_demo receiver pointcloud" 2>/dev/null || true
[ -f "output/test.pcd" ] && echo "✓ Point Cloud 通过" || echo "✗ Point Cloud 失败"

# 3. Grid Map Test
echo ""
echo "[3/4] Grid Map 测试..."
./build/multi_streaming_demo receiver gridmap 9003 output/test.grid &
sleep 1
timeout 5 ./build/multi_streaming_demo sender gridmap 127.0.0.1 9003 test 2>/dev/null
sleep 1
pkill -f "multi_streaming_demo receiver gridmap" 2>/dev/null || true
[ -f "output/test.grid" ] && echo "✓ Grid Map 通过" || echo "✗ Grid Map 失败"

# 4. Voice Test
echo ""
echo "[4/4] Voice 测试..."
if [ -f "data/voice/test_8k.wav" ]; then
    ./build/voice_demo receiver 9004 output/test_out.wav &
    sleep 1
    timeout 10 ./build/voice_demo sender 127.0.0.1 9004 data/voice/test_8k.wav 2>/dev/null
    sleep 1
    pkill -f "voice_demo receiver" 2>/dev/null || true
    [ -f "output/test_out.wav" ] && echo "✓ Voice 通过" || echo "✗ Voice 失败"
else
    echo "⊘ Voice 测试数据不存在 (data/voice/test_8k.wav)"
fi

echo ""
echo "=========================================="
echo "  测试完成"
echo "=========================================="

# 清理
pkill -9 -f "multi_streaming_demo\|voice_demo" 2>/dev/null || true
