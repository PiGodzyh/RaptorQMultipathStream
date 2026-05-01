#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== 1. 编译 ==="
make raptorq_demo 2>&1 | tail -5

echo ""
echo "=== 2. 清理旧进程和旧文件 ==="
ps aux | grep "[r]aptorq_demo" | awk '{print $2}' | xargs -r kill -9 2>/dev/null || true
sleep 1
mkdir -p logs/normal
rm -f output/videos/received.mp4

echo ""
echo "=== 3. 启动接收端 ==="
./build/raptorq_demo receiver > logs/normal/receiver.log 2>&1 &
RECV_PID=$!
echo "Receiver PID: $RECV_PID"
sleep 2

echo ""
echo "=== 4. 启动发送端 ==="
./build/raptorq_demo sender 127.0.0.1 all > logs/normal/sender.log 2>&1 &
SEND_PID=$!
echo "Sender PID: $SEND_PID"

echo ""
echo "=== 5. 等待数据收发完成（约14秒）==="
sleep 14

echo ""
echo "=== 6. 发送 SIGTERM 给接收端，触发 CloseOutputFile ==="
kill -TERM "$RECV_PID" 2>/dev/null || true
sleep 3

echo ""
echo "=== 7. 检查 MP4 文件 ==="
ls -lh output/videos/received.mp4
ffprobe -v error -select_streams v:0 -show_entries stream=nb_frames,r_frame_rate,duration -of default=noprint_wrappers=1 output/videos/received.mp4

echo ""
echo "=== 8. 检查帧顺序（全局）==="
ffprobe -v error -select_streams v:0 -show_entries frame=pkt_dts_time -of default=noprint_wrappers=1 output/videos/received.mp4 | \
    awk -F'=' '{print $2}' | \
    awk 'BEGIN{last=-1} $1+0==$1 {if($1+0 < last-0.0001){print "乱序: frame " NR " " last " -> " $1} last=$1}' | \
    head -5
[ "$(ffprobe -v error -select_streams v:0 -show_entries frame=pkt_dts_time -of default=noprint_wrappers=1 output/videos/received.mp4 | \
    awk -F'=' '{print $2}' | \
    awk 'BEGIN{last=-1} $1+0==$1 {if($1+0 < last-0.0001){count++} last=$1} END{print count}')" -eq 0 ] && echo "✓ 全部帧严格递增，无乱序" || echo "✗ 发现乱序"

echo ""
echo "=== 9. 重点检查 8s→9s 过渡 ==="
ffprobe -v error -select_streams v:0 -show_entries frame=pkt_dts_time -of default=noprint_wrappers=1 output/videos/received.mp4 | \
    awk -F'=' '{print $2}' | \
    awk -v start=7.8 -v end=9.2 '$1>=start && $1<=end {print NR-1 ": " $1}' | \
    head -50

echo ""
echo "=== 10. 清理残留进程 ==="
ps -p "$RECV_PID" > /dev/null 2>&1 && kill -9 "$RECV_PID" 2>/dev/null || true
ps aux | grep "[r]aptorq_demo sender" | awk '{print $2}' | xargs -r kill -9 2>/dev/null || true

echo ""
echo "=== 完成 ==="
