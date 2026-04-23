#!/bin/bash
# H.264 实时视频浏览器观看启动脚本
# 使用方法: ./start_live_view.sh [发送端IP] [视频文件名]
#   默认发送端IP: 127.0.0.1 (本地测试)
#   默认视频文件: test.mp4

cd "$(dirname "$0")"

SENDER_IP="${1:-127.0.0.1}"
VIDEO_FILE="${2:-test_gop1s.mp4}"
HTTP_PORT=8080

# 清理旧进程
pkill -f "live_http_server.py" 2>/dev/null
pkill -f "multi_streaming_demo" 2>/dev/null
sleep 1

rm -f /tmp/video_live.h264
rm -f /tmp/receiver.log /tmp/sender.log

echo "========================================"
echo "  实时视频流 - 浏览器观看模式"
echo "========================================"
echo "  发送端: $SENDER_IP"
echo "  视频文件: data/videos/$VIDEO_FILE"
echo "  HTTP 端口: $HTTP_PORT"
echo ""

# 1. 启动接收端
echo "[1/4] 启动接收端 (端口 9001)..."
nohup ./build/multi_streaming_demo receiver video 9001 output_recv.mp4 > /tmp/receiver.log 2>&1 &
RECV_PID=$!
disown
sleep 2

# 2. 启动 HTTP 服务器
echo "[2/4] 启动 HTTP MJPEG 流媒体服务器 (端口 $HTTP_PORT)..."
nohup python3 live_http_server.py > /tmp/http_server.log 2>&1 &
HTTP_PID=$!
disown
sleep 2

# 3. 检查服务状态
echo "[3/4] 检查服务状态..."
if curl -s -o /dev/null -w "%{http_code}" http://localhost:$HTTP_PORT/ | grep -q "200"; then
    echo "  ✅ HTTP 服务器运行正常"
else
    echo "  ⚠️ HTTP 服务器可能未就绪，继续..."
fi

# 4. 启动发送端
echo "[4/4] 启动发送端 (发送至 $SENDER_IP:9001)..."
nohup ./build/multi_streaming_demo sender video $SENDER_IP 9001 "$VIDEO_FILE" > /tmp/sender.log 2>&1 &
SEND_PID=$!
disown

# 检测是否在 WSL 中运行
IS_WSL=false
WSL_VERSION=""
if grep -qi microsoft /proc/sys/kernel/osrelease 2>/dev/null; then
    IS_WSL=true
    if grep -q "WSL2" /proc/sys/kernel/osrelease 2>/dev/null; then
        WSL_VERSION="2"
    else
        WSL_VERSION="1"
    fi
fi

# 获取本机 IP
LOCAL_IP=$(ip route get 1.1.1.1 2>/dev/null | grep -oP 'src \K\S+' || hostname -I | awk '{print $1}')

echo ""
echo "========================================"
echo "  ✅ 所有服务已启动"
echo "========================================"

if [ "$IS_WSL" = true ]; then
    echo "  检测到 WSL$WSL_VERSION 环境"
    echo ""
    echo "  📖 Windows 浏览器请打开:"
    echo "     http://$LOCAL_IP:$HTTP_PORT"
    echo ""
    echo "  📖 如果上面打不开，试试:"
    echo "     http://127.0.0.1:$HTTP_PORT"
    echo "     http://localhost:$HTTP_PORT"
    echo ""
    echo "  提示: WSL2 使用独立虚拟网卡，"
    echo "       Windows 浏览器通过上述 IP 访问 WSL2 内部服务"
else
    echo "  浏览器访问地址:"
    echo "     http://$LOCAL_IP:$HTTP_PORT"
    echo "     http://127.0.0.1:$HTTP_PORT"
fi

echo ""
echo "  各进程 PID:"
echo "    接收端: $RECV_PID"
echo "    HTTP 服务: $HTTP_PID"
echo "    发送端: $SEND_PID"
echo "========================================"
echo ""
echo "按 Ctrl+C 停止所有服务"
echo ""

# 等待中断
trap 'echo ""; echo "正在停止所有服务..."; kill $RECV_PID $HTTP_PID $SEND_PID 2>/dev/null; pkill -f "live_http_server.py" 2>/dev/null; pkill -f "multi_streaming_demo" 2>/dev/null; echo "已停止"; exit 0' INT TERM
wait
