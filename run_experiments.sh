#!/bin/bash
# run_experiments.sh - RaptorQ 对比实验一键测试脚本
#
# 用法:
#   ./run_experiments.sh normal              # 正常 RaptorQ 模式
#   ./run_experiments.sh bypass              # Bypass FEC 模式
#   ./run_experiments.sh redundancy 0.5      # 冗余度调节 (0.0~1.0)
#   ./run_experiments.sh all                 # 顺序运行 normal + bypass
#
# 网络模拟 (可选):
#   LOSS=10 ./run_experiments.sh normal      # 10% 丢包 (lo 实际约 19%)
#   LOSS=20 ./run_experiments.sh bypass      # 20% 丢包 (lo 实际约 36%)
#
# 日志输出位置:
#   logs/normal/rx.log, logs/normal/tx.log
#   logs/bypass/rx.log, logs/bypass/tx.log
#   logs/redundancy/rx_r{ratio}.log, logs/redundancy/tx_r{ratio}.log

set -e

WORK_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$WORK_DIR"

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 测试参数
TEST_DURATION=45          # 测试时长(秒)，视频约43秒
RECEIVER_WARMUP=3         # receiver 启动等待时间
RECEIVER_SHUTDOWN=3       # receiver 关闭等待时间
LOSS_RATE="${LOSS:-}"     # 网络丢包率 (0~100)，空表示不模拟

# ============================================================================
# 工具函数
# ============================================================================

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_ok() {
    echo -e "${GREEN}[OK]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_err() {
    echo -e "${RED}[ERR]${NC} $1"
}

cleanup_processes() {
    log_info "清理残留进程..."
    pkill -f raptorq_demo 2>/dev/null || true
    sleep 1
    pkill -9 -f raptorq_demo 2>/dev/null || true
    sleep 1
}

setup_network() {
    if [ -n "$LOSS_RATE" ]; then
        log_info "设置网络模拟: lo 丢包率 ${LOSS_RATE}% (实际效果约 $(awk "BEGIN {printf \"%.0f\", (1-(1-$LOSS_RATE/100)^2)*100}")%)"
        sudo ./setup_network.sh loss "$LOSS_RATE"
    fi
}

cleanup_network() {
    if [ -n "$LOSS_RATE" ]; then
        log_info "清除网络模拟规则..."
        sudo ./setup_network.sh clean 2>/dev/null || true
    fi
}

wait_for_sender() {
    local log_file="$1"
    local timeout_sec="${2:-60}"
    local waited=0
    while [ $waited -lt $timeout_sec ]; do
        if grep -q "Send thread finished" "$log_file" 2>/dev/null; then
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
    done
    log_warn "Sender 未在 ${timeout_sec}s 内结束，继续..."
}

extract_stat() {
    local log_file="$1"
    local pattern="$2"
    grep -E "$pattern" "$log_file" 2>/dev/null | tail -1 || echo "N/A"
}

# ============================================================================
# 测试执行函数
# ============================================================================

run_normal_test() {
    echo ""
    echo "========================================"
    echo "  测试: 正常 RaptorQ 模式"
    echo "========================================"

    local rx_log="logs/normal/rx.log"
    local tx_log="logs/normal/tx.log"

    mkdir -p logs/normal
    cleanup_processes
    rm -f output/videos/received.mp4 "$rx_log" "$tx_log"

    setup_network
    setup_network
    log_info "启动 Receiver..."
    stdbuf -o0 ./build/raptorq_demo receiver > "$rx_log" 2>&1 &
    local rx_pid=$!
    sleep $RECEIVER_WARMUP

    log_info "启动 Sender (视频)..."
    stdbuf -o0 ./build/raptorq_demo sender 127.0.0.1 video > "$tx_log" 2>&1 &
    local tx_pid=$!

    log_info "等待传输完成 (约 ${TEST_DURATION}s)..."
    wait_for_sender "$tx_log" $((TEST_DURATION + 20))

    log_info "关闭 Receiver..."
    kill -TERM $rx_pid 2>/dev/null || true
    sleep $RECEIVER_SHUTDOWN
    kill -9 $rx_pid 2>/dev/null || true
    cleanup_network

    echo ""
    echo "---------- 结果汇总 ----------"

    # MP4 文件检查
    if [ -f "output/videos/received.mp4" ]; then
        local mp4_size=$(ls -lh output/videos/received.mp4 | awk '{print $5}')
        log_ok "MP4 文件生成: ${mp4_size}"
        local ffprobe_out=$(ffprobe -v error -select_streams v:0 -show_entries stream=nb_frames,duration -of default=noprint_wrappers=1 output/videos/received.mp4 2>/dev/null || true)
        echo "  ${ffprobe_out}"
    else
        log_err "MP4 文件未生成!"
    fi

    # Sender 统计
    local sent_frames=$(extract_stat "$tx_log" "sent.*frames")
    echo "  Sender: ${sent_frames}"

    # Receiver 统计
    local recv_stat=$(extract_stat "$rx_log" "Video\s+[0-9]+")
    echo "  Receiver ${recv_stat}"

    # 日志位置
    echo ""
    log_info "日志位置:"
    echo "  Receiver: ${rx_log}"
    echo "  Sender:   ${tx_log}"
}

run_bypass_test() {
    echo ""
    echo "========================================"
    echo "  测试: Bypass FEC 模式"
    echo "========================================"

    local rx_log="logs/bypass/rx.log"
    local tx_log="logs/bypass/tx.log"

    mkdir -p logs/bypass
    cleanup_processes
    rm -f output/videos/received.mp4 "$rx_log" "$tx_log"

    setup_network
    log_info "启动 Receiver (bypass)..."
    stdbuf -o0 ./build/raptorq_demo receiver --bypass-fec > "$rx_log" 2>&1 &
    local rx_pid=$!
    sleep $RECEIVER_WARMUP

    log_info "启动 Sender (bypass)..."
    stdbuf -o0 ./build/raptorq_demo sender 127.0.0.1 video --bypass-fec > "$tx_log" 2>&1 &
    local tx_pid=$!

    log_info "等待传输完成 (约 ${TEST_DURATION}s)..."
    wait_for_sender "$tx_log" $((TEST_DURATION + 20))

    log_info "关闭 Receiver..."
    kill -TERM $rx_pid 2>/dev/null || true
    sleep $RECEIVER_SHUTDOWN
    kill -9 $rx_pid 2>/dev/null || true
    cleanup_network

    echo ""
    echo "---------- 结果汇总 ----------"

    # MP4 文件检查
    if [ -f "output/videos/received.mp4" ]; then
        local mp4_size=$(ls -lh output/videos/received.mp4 | awk '{print $5}')
        log_warn "MP4 文件意外生成: ${mp4_size} (bypass 模式不应生成)"
    else
        log_ok "MP4 文件未生成 (符合预期)"
    fi

    # Sender 统计
    local sent_frames=$(extract_stat "$tx_log" "sent.*frames")
    echo "  Sender: ${sent_frames}"

    # Receiver 统计
    local recv_stat=$(extract_stat "$rx_log" "Video\s+[0-9]+")
    echo "  Receiver ${recv_stat}"

    # 丢包率估算
    local sent_num=$(echo "$sent_frames" | grep -oE '[0-9]+' | tail -1 || echo "0")
    local recv_num=$(echo "$recv_stat" | grep -oE '[0-9]+' | head -1 || echo "0")
    if [ "$sent_num" -gt 0 ] 2>/dev/null; then
        local loss=$((sent_num - recv_num))
        local loss_rate=$(awk "BEGIN {printf \"%.1f\", $loss / $sent_num * 100}")
        echo "  估算丢包: ${loss} 帧 (${loss_rate}%)"
    fi

    echo ""
    log_info "日志位置:"
    echo "  Receiver: ${rx_log}"
    echo "  Sender:   ${tx_log}"
}

run_redundancy_test() {
    local ratio="$1"
    echo ""
    echo "========================================"
    echo "  测试: 冗余度调节 ${ratio}"
    echo "========================================"

    local rx_log="logs/redundancy/rx_r${ratio}.log"
    local tx_log="logs/redundancy/tx_r${ratio}.log"

    mkdir -p logs/redundancy
    cleanup_processes
    rm -f output/videos/received.mp4 "$rx_log" "$tx_log"

    log_info "启动 Receiver..."
    stdbuf -o0 ./build/raptorq_demo receiver > "$rx_log" 2>&1 &
    local rx_pid=$!
    sleep $RECEIVER_WARMUP

    log_info "启动 Sender (冗余度 ${ratio})..."
    stdbuf -o0 ./build/raptorq_demo sender 127.0.0.1 video --redundancy "$ratio" > "$tx_log" 2>&1 &
    local tx_pid=$!

    log_info "等待传输完成 (约 ${TEST_DURATION}s)..."
    wait_for_sender "$tx_log" $((TEST_DURATION + 20))

    log_info "关闭 Receiver..."
    kill -TERM $rx_pid 2>/dev/null || true
    sleep $RECEIVER_SHUTDOWN
    kill -9 $rx_pid 2>/dev/null || true
    cleanup_network

    echo ""
    echo "---------- 结果汇总 ----------"

    # MP4 文件检查
    if [ -f "output/videos/received.mp4" ]; then
        local mp4_size=$(ls -lh output/videos/received.mp4 | awk '{print $5}')
        log_ok "MP4 文件生成: ${mp4_size}"
    else
        log_err "MP4 文件未生成!"
    fi

    # Sender 统计
    local sent_frames=$(extract_stat "$tx_log" "sent.*frames")
    echo "  Sender: ${sent_frames}"

    # Receiver 统计
    local recv_stat=$(extract_stat "$rx_log" "Video\s+[0-9]+")
    echo "  Receiver ${recv_stat}"

    echo ""
    log_info "日志位置:"
    echo "  Receiver: ${rx_log}"
    echo "  Sender:   ${tx_log}"
}

# ============================================================================
# 主入口
# ============================================================================

print_usage() {
    echo "用法: $0 <mode> [args]"
    echo ""
    echo "模式:"
    echo "  normal                    正常 RaptorQ 模式"
    echo "  bypass                    Bypass FEC 模式"
    echo "  redundancy <ratio>        冗余度调节 (0.0~1.0)"
    echo "  all                       顺序运行 normal + bypass"
    echo ""
    echo "示例:"
    echo "  $0 normal"
    echo "  $0 bypass"
    echo "  $0 redundancy 0.5"
    echo "  $0 all"
}

# 检查编译
if [ ! -f "build/raptorq_demo" ]; then
    log_err "build/raptorq_demo 不存在，请先编译: make raptorq_demo"
    exit 1
fi

MODE="${1:-}"
case "$MODE" in
    normal)
        run_normal_test
        ;;
    bypass)
        run_bypass_test
        ;;
    redundancy)
        RATIO="${2:-}"
        if [ -z "$RATIO" ]; then
            log_err "缺少冗余度参数"
            print_usage
            exit 1
        fi
        run_redundancy_test "$RATIO"
        ;;
    all)
        run_normal_test
        run_bypass_test
        echo ""
        echo "========================================"
        log_ok "全部测试完成"
        echo "========================================"
        ;;
    *)
        print_usage
        exit 1
        ;;
esac

echo ""
log_info "所有日志保存在 logs/ 目录下，git 已忽略"
