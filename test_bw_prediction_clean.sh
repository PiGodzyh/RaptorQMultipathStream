#!/bin/bash
# 验证 clean 网络下带宽预测不会过度限制

set -e

echo "========================================"
echo "  Clean 网络测试（无限速）"
echo "========================================"

# 1. 确保没有 tc 规则
echo "[1/5] 清理 tc 规则..."
sudo tc qdisc del dev lo root 2>/dev/null || true

# 2. 清理旧日志
echo "[2/5] 清理旧日志..."
rm -f logs/fc_tx.log logs/video_tx.log logs/video_rx.log output/fc/received.log
mkdir -p logs output/fc

# 3. 启动接收端
echo "[3/5] 启动接收端..."
./build/raptorq_demo receiver > logs/receiver.log 2>&1 &
RX_PID=$!
sleep 2

# 4. 启动发送端
echo "[4/5] 启动发送端 (fc + video)..."
(
    for i in $(seq 1 300); do
        echo "CMD_$i"
        sleep 0.1
    done
    echo "quit"
) | ./build/raptorq_demo sender 127.0.0.1 fc,video > logs/sender.log 2>&1 &
TX_PID=$!

# 5. 运行 30 秒
echo "[5/5] 运行 30 秒..."
sleep 30

# 停止
echo "停止进程..."
kill -INT $TX_PID 2>/dev/null || true
kill -INT $RX_PID 2>/dev/null || true
sleep 3
kill -9 $TX_PID 2>/dev/null || true
kill -9 $RX_PID 2>/dev/null || true

# 分析
echo ""
echo "========================================"
echo "  Clean 网络测试结果"
echo "========================================"

FC_SENT=$(wc -l < logs/fc_tx.log 2>/dev/null || echo 0)
FC_RECV=$(wc -l < output/fc/received.log 2>/dev/null || echo 0)
echo "FC 发送: $FC_SENT, 接收: $FC_RECV"
if [ "$FC_SENT" -gt 0 ]; then
    ARRIVAL=$(echo "scale=1; $FC_RECV * 100 / $FC_SENT" | bc)
    echo "FC 到达率: ${ARRIVAL}%"
fi

VIDEO_SENT=$(wc -l < logs/video_tx.log 2>/dev/null || echo 0)
VIDEO_RECV=$(wc -l < logs/video_rx.log 2>/dev/null || echo 0)
echo "Video 发送: $VIDEO_SENT, 接收: $VIDEO_RECV"

echo ""
echo "--- Feedback 调整日志 ---"
grep "FeedbackController.*Adjusted" logs/sender.log 2>/dev/null | tail -10 || echo "(无)"

echo ""
echo "========================================"
