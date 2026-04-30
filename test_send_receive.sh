#!/bin/bash
# test_send_receive.sh - 一键测试发送→接收→保存文件
# 用法: ./test_send_receive.sh [video|voice|pointcloud|gridmap|fc|all]

set -e

cd "$(dirname "$0")"
mkdir -p output

# 颜色
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

TEST_TYPE="${1:-all}"
TARGET_IP="${2:-127.0.0.1}"

# 清理函数
cleanup() {
    pkill -f "video_streaming_demo receiver" 2>/dev/null || true
    pkill -f "voice_demo receiver" 2>/dev/null || true
    pkill -f "multi_streaming_demo receiver" 2>/dev/null || true
    sleep 0.5
}

trap cleanup EXIT

# ==============================================================================
# 测试视频传输 (video_streaming_demo)
# ==============================================================================
test_video() {
    echo ""
    echo "=========================================="
    echo "  测试: 视频传输"
    echo "=========================================="
    
    local input="data/videos/test_gop1s.mp4"
    local output="output/received_video.mp4"
    
    if [ ! -f "$input" ]; then
        echo -e "${YELLOW}⊘ 跳过: 测试视频不存在 ($input)${NC}"
        return 0
    fi
    
    rm -f "$output"
    
    # 启动接收端
    ./build/video_streaming_demo receiver 9001 "$output" &
    local recv_pid=$!
    sleep 2
    
    # 启动发送端
    echo "[发送] $input → $TARGET_IP:9001"
    timeout 30 ./build/video_streaming_demo sender "$TARGET_IP" 9001 "$input" || true
    
    sleep 2
    kill $recv_pid 2>/dev/null || true
    wait $recv_pid 2>/dev/null || true
    
    # 检查输出
    if [ -f "$output" ] && [ -s "$output" ]; then
        local out_size=$(du -h "$output" | cut -f1)
        echo -e "${GREEN}✓ 视频传输成功${NC}  输出: $output ($out_size)"
        return 0
    else
        echo -e "${RED}✗ 视频传输失败${NC}  输出文件不存在或为空"
        return 1
    fi
}

# ==============================================================================
# 测试语音传输 (voice_demo)
# ==============================================================================
test_voice() {
    echo ""
    echo "=========================================="
    echo "  测试: 语音传输"
    echo "=========================================="
    
    local input="data/voice/test_8k.wav"
    local output="output/received_voice.wav"
    
    if [ ! -f "$input" ]; then
        echo -e "${YELLOW}⊘ 跳过: 测试音频不存在 ($input)${NC}"
        return 0
    fi
    
    rm -f "$output"
    
    # 启动接收端
    ./build/voice_demo receive 9004 "$output" &
    local recv_pid=$!
    sleep 1
    
    # 启动发送端
    echo "[发送] $input → $TARGET_IP:9004"
    timeout 15 ./build/voice_demo sender "$TARGET_IP" 9004 "$input" || true
    
    sleep 2
    kill $recv_pid 2>/dev/null || true
    wait $recv_pid 2>/dev/null || true
    
    # 检查输出
    if [ -f "$output" ] && [ -s "$output" ]; then
        local out_size=$(du -h "$output" | cut -f1)
        echo -e "${GREEN}✓ 语音传输成功${NC}  输出: $output ($out_size)"
        return 0
    else
        echo -e "${RED}✗ 语音传输失败${NC}  输出文件不存在或为空"
        return 1
    fi
}

# ==============================================================================
# 测试点云传输 (multi_streaming_demo)
# ==============================================================================
test_pointcloud() {
    echo ""
    echo "=========================================="
    echo "  测试: 点云传输"
    echo "=========================================="
    
    local output="output/received_pointcloud.pcd"
    rm -f "$output"
    
    # 启动接收端（禁用PCL可视化，避免弹窗阻塞）
    # 注: multi_streaming_demo receiver pointcloud 目前没有禁用PCL的选项
    # 使用 timeout 限制运行时间
    timeout 15 ./build/multi_streaming_demo receiver pointcloud 9002 "$output" &
    local recv_pid=$!
    sleep 2
    
    # 启动发送端
    echo "[发送] 测试点云 → $TARGET_IP:9002"
    timeout 10 ./build/multi_streaming_demo sender pointcloud "$TARGET_IP" 9002 test || true
    
    sleep 3
    kill $recv_pid 2>/dev/null || true
    wait $recv_pid 2>/dev/null || true
    
    # 检查输出
    if [ -f "$output" ] && [ -s "$output" ]; then
        local out_size=$(du -h "$output" | cut -f1)
        echo -e "${GREEN}✓ 点云传输成功${NC}  输出: $output ($out_size)"
        return 0
    else
        echo -e "${RED}✗ 点云传输失败${NC}  输出文件不存在或为空"
        return 1
    fi
}

# ==============================================================================
# 测试栅格地图传输 (multi_streaming_demo)
# ==============================================================================
test_gridmap() {
    echo ""
    echo "=========================================="
    echo "  测试: 栅格地图传输"
    echo "=========================================="
    
    local output="output/received_gridmap.grid"
    rm -f "$output"
    
    # 启动接收端
    timeout 15 ./build/multi_streaming_demo receiver gridmap 9003 "$output" &
    local recv_pid=$!
    sleep 2
    
    # 启动发送端
    echo "[发送] 测试栅格地图 → $TARGET_IP:9003"
    timeout 10 ./build/multi_streaming_demo sender gridmap "$TARGET_IP" 9003 test || true
    
    sleep 3
    kill $recv_pid 2>/dev/null || true
    wait $recv_pid 2>/dev/null || true
    
    # 检查输出
    if [ -f "$output" ] && [ -s "$output" ]; then
        local out_size=$(du -h "$output" | cut -f1)
        echo -e "${GREEN}✓ 栅格地图传输成功${NC}  输出: $output ($out_size)"
        return 0
    else
        echo -e "${RED}✗ 栅格地图传输失败${NC}  输出文件不存在或为空"
        return 1
    fi
}

# ==============================================================================
# 测试飞控指令 (multi_streaming_demo)
# ==============================================================================
test_fc() {
    echo ""
    echo "=========================================="
    echo "  测试: 飞控指令传输"
    echo "=========================================="
    
    local output="output/received_fc.log"
    rm -f "$output"
    
    # 启动接收端
    timeout 10 ./build/multi_streaming_demo receiver fc 9000 "$output" &
    local recv_pid=$!
    sleep 1
    
    # 启动发送端（非交互模式，直接发送几条指令）
    echo "[发送] 飞控指令 → $TARGET_IP:9000"
    echo -e "TAKEOFF\nMOVE 1.0 2.0 3.0\nLAND\nquit" | timeout 5 ./build/multi_streaming_demo sender fc "$TARGET_IP" 9000 || true
    
    sleep 2
    kill $recv_pid 2>/dev/null || true
    wait $recv_pid 2>/dev/null || true
    
    # 检查输出
    if [ -f "$output" ] && [ -s "$output" ]; then
        local lines=$(wc -l < "$output")
        echo -e "${GREEN}✓ 飞控指令传输成功${NC}  输出: $output ($lines 行)"
        return 0
    else
        echo -e "${RED}✗ 飞控指令传输失败${NC}  输出文件不存在或为空"
        return 1
    fi
}

# ==============================================================================
# 主程序
# ==============================================================================
echo "=========================================="
echo "  发送→接收→保存文件 一键测试"
echo "=========================================="
echo "目标IP: $TARGET_IP"
echo ""

# 先清理
pkill -9 -f "video_streaming_demo\|voice_demo\|multi_streaming_demo" 2>/dev/null || true
sleep 1

passed=0
failed=0

case "$TEST_TYPE" in
    video|v)
        test_video && ((passed++)) || ((failed++))
        ;;
    voice|audio|a)
        test_voice && ((passed++)) || ((failed++))
        ;;
    pointcloud|pc|p)
        test_pointcloud && ((passed++)) || ((failed++))
        ;;
    gridmap|grid|g)
        test_gridmap && ((passed++)) || ((failed++))
        ;;
    fc|f)
        test_fc && ((passed++)) || ((failed++))
        ;;
    all|*)
        test_video && ((passed++)) || ((failed++))
        test_voice && ((passed++)) || ((failed++))
        test_pointcloud && ((passed++)) || ((failed++))
        test_gridmap && ((passed++)) || ((failed++))
        test_fc && ((passed++)) || ((failed++))
        ;;
esac

echo ""
echo "=========================================="
echo "  测试结果"
echo "=========================================="
echo -e "${GREEN}通过: $passed${NC}"
echo -e "${RED}失败: $failed${NC}"
echo ""

if [ $failed -eq 0 ]; then
    echo -e "${GREEN}全部测试通过!${NC}"
    exit 0
else
    echo -e "${RED}部分测试失败，请检查日志${NC}"
    exit 1
fi
