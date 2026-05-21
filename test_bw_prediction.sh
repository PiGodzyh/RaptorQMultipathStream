#!/bin/bash
# 测试基于 feedback 的带宽预测在 3mbit 限速下的效果

set -e

echo "========================================"
echo "  带宽预测测试 (3mbit tc netem)"
echo "========================================"

# 1. 清理旧的 tc 规则
echo "[1/6] 清理旧的 tc 规则..."
sudo tc qdisc del dev lo root 2>/dev/null || true

# 2. 设置 3mbit 限速
echo "[2/6] 设置 tc netem rate 3mbit..."
sudo tc qdisc add dev lo root netem rate 3mbit
sudo tc qdisc show dev lo

# 3. 清理旧日志
echo "[3/6] 清理旧日志..."
rm -f logs/fc_tx.log logs/video_tx.log logs/video_rx.log output/fc/received.log
mkdir -p logs output/fc

# 4. 启动接收端 (后台)
echo "[4/6] 启动接收端..."
./build/raptorq_demo receiver > logs/receiver.log 2>&1 &
RX_PID=$!
echo "  Receiver PID: $RX_PID"
sleep 2

# 5. 启动发送端 (fc + video)，后台运行，FC 通过管道自动输入
echo "[5/6] 启动发送端 (fc + video)..."
# 每 100ms 发送一条 FC 命令，共 300 条（30秒）
(
    for i in $(seq 1 300); do
        echo "CMD_$i"
        sleep 0.1
    done
    echo "quit"
) | ./build/raptorq_demo sender 127.0.0.1 fc,video > logs/sender.log 2>&1 &
TX_PID=$!
echo "  Sender PID: $TX_PID"

# 6. 等待 30 秒
echo "[6/6] 运行 30 秒..."
sleep 30

# 7. 停止进程 (发送 SIGINT 使 g_running=false，再强制终止)
echo "停止进程..."
kill -INT $TX_PID 2>/dev/null || true
kill -INT $RX_PID 2>/dev/null || true
sleep 3
kill -9 $TX_PID 2>/dev/null || true
kill -9 $RX_PID 2>/dev/null || true

# 8. 分析结果
echo ""
echo "========================================"
echo "  测试结果分析"
echo "========================================"

# FC 发送数
if [ -f logs/fc_tx.log ]; then
    FC_SENT=$(wc -l < logs/fc_tx.log)
    echo "FC 发送: $FC_SENT 条"
else
    echo "FC 发送日志未找到"
    FC_SENT=0
fi

# FC 接收数
if [ -f output/fc/received.log ]; then
    FC_RECV=$(wc -l < output/fc/received.log)
    echo "FC 接收: $FC_RECV 条"
else
    echo "FC 接收日志未找到"
    FC_RECV=0
fi

# 到达率
if [ "$FC_SENT" -gt 0 ]; then
    ARRIVAL=$(echo "scale=1; $FC_RECV * 100 / $FC_SENT" | bc)
    echo "FC 到达率: ${ARRIVAL}%"
else
    echo "FC 到达率: N/A"
fi

# 平均延迟
if [ -f output/fc/received.log ]; then
    AVG_DELAY=$(awk -F',' '{sum+=$4; count++} END {if(count>0) printf "%.3f", sum/count}' output/fc/received.log)
    echo "FC 平均延迟: ${AVG_DELAY}ms"
fi

# Video 统计
if [ -f logs/video_tx.log ]; then
    VIDEO_SENT=$(wc -l < logs/video_tx.log)
    echo "Video 发送: $VIDEO_SENT 帧"
else
    echo "Video 发送日志未找到"
fi

if [ -f logs/video_rx.log ]; then
    VIDEO_RECV=$(wc -l < logs/video_rx.log)
    echo "Video 接收: $VIDEO_RECV 帧"
else
    echo "Video 接收日志未找到"
fi

# Feedback 调整日志
echo ""
echo "--- Feedback 速率调整日志 ---"
grep "FeedbackController.*Adjusted" logs/sender.log 2>/dev/null | tail -10 || echo "(无调整日志)"

echo ""
echo "========================================"
echo "  测试完成"
echo "========================================"

# 清理 tc 规则
sudo tc qdisc del dev lo root 2>/dev/null || true
