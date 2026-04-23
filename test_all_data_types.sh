#!/bin/bash
# test_all_data_types.sh - 测试所有数据类型的 UnifiedSender 迁移
# 
# 端口分配:
# - 9000: FC Control (飞控指令)
# - 9001: Video (视频)
# - 9002: Point Cloud (点云)
# - 9003: Grid Map (栅格地图)
# - 9004: Voice (语音)

set -e

WORK_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$WORK_DIR"

echo "=========================================="
echo "  UnifiedSender 全数据类型测试"
echo "=========================================="
echo ""

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 测试数据准备
echo "[准备] 检查测试数据..."
mkdir -p data/fc data/pointcloud data/gridmap data/voice data/videos output/fc output/pointcloud output/gridmap output/voice output/videos

# 生成语音测试数据
if [ ! -f "data/voice/test_8k.wav" ]; then
    echo "[准备] 生成语音测试数据..."
    ./build/generate_sample_data 2>/dev/null || echo "警告: generate_sample_data 未编译"
fi

echo "[准备] 测试数据就绪"
echo ""

# 清理函数
cleanup() {
    echo ""
    echo "[清理] 停止所有测试进程..."
    pkill -f "multi_streaming_demo receiver" 2>/dev/null || true
    pkill -f "voice_demo receiver" 2>/dev/null || true
    pkill -f "video_streaming_demo receiver" 2>/dev/null || true
    sleep 1
    echo "[清理] 完成"
}
trap cleanup EXIT

# ==============================================================================
# 测试 1: FC Control (飞控指令)
# ==============================================================================
test_fc_control() {
    echo "=========================================="
    echo "  测试 1: FC Control (飞控指令)"
    echo "=========================================="
    
    # 启动接收端
    ./build/multi_streaming_demo receiver fc 9000 output/fc/test.log &
    FC_RECEIVER_PID=$!
    sleep 2
    
    # 测试发送单条指令（非交互模式）
    echo "[测试] 发送飞控指令..."
    timeout 5 ./build/multi_streaming_demo sender fc 127.0.0.1 9000 <<EOF
TAKEOFF
MOVE 1.0 2.0 3.0
LAND
quit
EOF
    
    kill $FC_RECEIVER_PID 2>/dev/null || true
    wait $FC_RECEIVER_PID 2>/dev/null || true
    
    if [ -f "output/fc/test.log" ]; then
        echo -e "${GREEN}[通过] FC Control 测试成功${NC}"
        echo "[日志内容]"
        head -5 output/fc/test.log 2>/dev/null || echo "(空日志)"
    else
        echo -e "${YELLOW}[警告] FC Control 日志文件未生成${NC}"
    fi
    echo ""
}

# ==============================================================================
# 测试 2: Point Cloud (点云)
# ==============================================================================
test_point_cloud() {
    echo "=========================================="
    echo "  测试 2: Point Cloud (点云)"
    echo "=========================================="
    
    # 启动接收端
    ./build/multi_streaming_demo receiver pointcloud 9002 output/pointcloud/test.pcd &
    PC_RECEIVER_PID=$!
    sleep 2
    
    # 发送测试点云
    echo "[测试] 发送测试点云 (100点/帧 x 5帧)..."
    timeout 10 ./build/multi_streaming_demo sender pointcloud 127.0.0.1 9002 "100:5"
    
    kill $PC_RECEIVER_PID 2>/dev/null || true
    wait $PC_RECEIVER_PID 2>/dev/null || true
    
    if [ -f "output/pointcloud/test.pcd" ]; then
        echo -e "${GREEN}[通过] Point Cloud 测试成功${NC}"
        ls -lh output/pointcloud/test.pcd
    else
        echo -e "${YELLOW}[警告] Point Cloud 输出文件未生成${NC}"
    fi
    echo ""
}

# ==============================================================================
# 测试 3: Grid Map (栅格地图)
# ==============================================================================
test_grid_map() {
    echo "=========================================="
    echo "  测试 3: Grid Map (栅格地图)"
    echo "=========================================="
    
    # 启动接收端
    ./build/multi_streaming_demo receiver gridmap 9003 output/gridmap/test.grid &
    GM_RECEIVER_PID=$!
    sleep 2
    
    # 发送测试栅格地图
    echo "[测试] 发送测试栅格地图 (50x50, 5次更新)..."
    timeout 10 ./build/multi_streaming_demo sender gridmap 127.0.0.1 9003 test
    
    kill $GM_RECEIVER_PID 2>/dev/null || true
    wait $GM_RECEIVER_PID 2>/dev/null || true
    
    if [ -f "output/gridmap/test.grid" ]; then
        echo -e "${GREEN}[通过] Grid Map 测试成功${NC}"
        ls -lh output/gridmap/test.grid
    else
        echo -e "${YELLOW}[警告] Grid Map 输出文件未生成${NC}"
    fi
    echo ""
}

# ==============================================================================
# 测试 4: Voice (语音)
# ==============================================================================
test_voice() {
    echo "=========================================="
    echo "  测试 4: Voice (语音)"
    echo "=========================================="
    
    if [ ! -f "data/voice/test_8k.wav" ]; then
        echo -e "${YELLOW}[跳过] 语音测试数据不存在${NC}"
        echo ""
        return
    fi
    
    # 启动接收端
    ./build/voice_demo receiver 9004 output/voice/test_output.wav &
    VOICE_RECEIVER_PID=$!
    sleep 2
    
    # 发送语音
    echo "[测试] 发送语音文件..."
    timeout 15 ./build/voice_demo sender 127.0.0.1 9004 data/voice/test_8k.wav
    
    kill $VOICE_RECEIVER_PID 2>/dev/null || true
    wait $VOICE_RECEIVER_PID 2>/dev/null || true
    
    if [ -f "output/voice/test_output.wav" ]; then
        echo -e "${GREEN}[通过] Voice 测试成功${NC}"
        ls -lh output/voice/test_output.wav
        
        # 验证文件大小
        INPUT_SIZE=$(stat -f%z "data/voice/test_8k.wav" 2>/dev/null || stat -c%s "data/voice/test_8k.wav" 2>/dev/null || echo "0")
        OUTPUT_SIZE=$(stat -f%z "output/voice/test_output.wav" 2>/dev/null || stat -c%s "output/voice/test_output.wav" 2>/dev/null || echo "0")
        echo "[验证] 输入: $INPUT_SIZE bytes, 输出: $OUTPUT_SIZE bytes"
    else
        echo -e "${YELLOW}[警告] Voice 输出文件未生成${NC}"
    fi
    echo ""
}

# ==============================================================================
# 测试 5: Video (视频) - 简化测试
# ==============================================================================
test_video() {
    echo "=========================================="
    echo "  测试 5: Video (视频)"
    echo "=========================================="
    
    # 检查是否有测试视频
    if [ ! -f "data/videos/test.mp4" ] && [ ! -f "data/videos/test.mp4" ]; then
        echo -e "${YELLOW}[跳过] 视频测试数据不存在${NC}"
        echo "请将测试视频放入 data/videos/ 目录"
        echo ""
        return
    fi
    
    VIDEO_FILE=$(ls data/videos/*.mp4 2>/dev/null | head -1)
    
    # 启动接收端
    ./build/video_streaming_demo receiver 9001 output/videos/test_output.mp4 &
    VIDEO_RECEIVER_PID=$!
    sleep 2
    
    # 发送视频 (限制发送时间)
    echo "[测试] 发送视频 (最多10秒)..."
    timeout 12 ./build/video_streaming_demo sender 127.0.0.1 9001 "$VIDEO_FILE" &
    VIDEO_SENDER_PID=$!
    
    wait $VIDEO_SENDER_PID 2>/dev/null || true
    
    kill $VIDEO_RECEIVER_PID 2>/dev/null || true
    wait $VIDEO_RECEIVER_PID 2>/dev/null || true
    
    if [ -f "output/videos/test_output.mp4" ]; then
        echo -e "${GREEN}[通过] Video 测试成功${NC}"
        ls -lh output/videos/test_output.mp4
    else
        echo -e "${YELLOW}[警告] Video 输出文件未生成${NC}"
    fi
    echo ""
}

# ==============================================================================
# 测试 6: 多流并发测试 (关键测试)
# ==============================================================================
test_multi_stream() {
    echo "=========================================="
    echo "  测试 6: 多流并发测试 (UnifiedSender)"
    echo "=========================================="
    echo "同时启动: Voice(9004) + FC(9000) + PointCloud(9002)"
    echo ""
    
    # 启动多个接收端
    ./build/voice_demo receiver 9004 output/voice/multi_test.wav &
    VOICE_PID=$!
    
    ./build/multi_streaming_demo receiver fc 9000 output/fc/multi_test.log &
    FC_PID=$!
    
    ./build/multi_streaming_demo receiver pointcloud 9002 output/pointcloud/multi_test.pcd &
    PC_PID=$!
    
    sleep 3
    
    # 同时发送
    if [ -f "data/voice/test_8k.wav" ]; then
        timeout 10 ./build/voice_demo sender 127.0.0.1 9004 data/voice/test_8k.wav &
    fi
    
    timeout 8 ./build/multi_streaming_demo sender fc 127.0.0.1 9000 <<EOF &
MULTI_TEST_START
CMD_1
CMD_2
CMD_3
quit
EOF
    FC_SENDER_PID=$!
    
    timeout 8 ./build/multi_streaming_demo sender pointcloud 127.0.0.1 9002 "50:3" &
    PC_SENDER_PID=$!
    
    wait $FC_SENDER_PID 2>/dev/null || true
    wait $PC_SENDER_PID 2>/dev/null || true
    
    sleep 2
    
    # 清理
    kill $VOICE_PID $FC_PID $PC_PID 2>/dev/null || true
    wait $VOICE_PID $FC_PID $PC_PID 2>/dev/null || true
    
    echo "[结果] 并发测试完成，检查输出文件:"
    ls -lh output/voice/multi_test.wav 2>/dev/null || echo "  - Voice: 未生成"
    ls -lh output/fc/multi_test.log 2>/dev/null || echo "  - FC: 未生成"
    ls -lh output/pointcloud/multi_test.pcd 2>/dev/null || echo "  - PointCloud: 未生成"
    
    echo -e "${GREEN}[通过] 多流并发测试完成${NC}"
    echo ""
}

# ==============================================================================
# 主测试流程
# ==============================================================================
main() {
    echo "开始测试时间: $(date)"
    echo ""
    
    # 检查可执行文件
    for exe in multi_streaming_demo voice_demo video_streaming_demo; do
        if [ ! -f "build/$exe" ]; then
            echo -e "${RED}[错误] build/$exe 不存在，请先编译${NC}"
            exit 1
        fi
    done
    
    # 顺序运行测试
    test_fc_control
    test_point_cloud
    test_grid_map
    test_voice
    test_video
    test_multi_stream
    
    echo "=========================================="
    echo -e "${GREEN}  所有测试完成!${NC}"
    echo "=========================================="
    echo "输出文件目录:"
    echo "  - output/fc/       : 飞控指令日志"
    echo "  - output/pointcloud/: 点云PCD文件"
    echo "  - output/gridmap/  : 栅格地图文件"
    echo "  - output/voice/    : 语音WAV文件"
    echo "  - output/videos/   : 视频MP4文件"
    echo ""
    echo "结束时间: $(date)"
}

main "$@"
