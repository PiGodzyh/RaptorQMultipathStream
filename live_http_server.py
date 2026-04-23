#!/usr/bin/env python3
"""
极简 H.264 实时视频 HTTP 服务器

原理: ffmpeg 读取 /tmp/video_live.h264 管道 → 解码为 MJPEG → 
      Python 分割 JPEG 帧 → HTTP multipart/x-mixed-replace 推流

依赖: 只需系统自带的 Python3 + ffmpeg（都已安装）
用法: python3 live_http_server.py
      浏览器打开 http://服务器IP:8080
"""

import os
import subprocess
import socketserver
import http.server
import sys

FIFO_PATH = '/tmp/video_live.h264'
PORT = 8080


def drain_fifo(path):
    """清空 FIFO 中积累的旧数据。
    
    当用户打开浏览器较晚时，管道中可能已经积累了数秒钟的旧帧。
    如果不清空，ffmpeg 会先处理这些旧帧，导致浏览器看到的画面从"几秒钟之后"开始。
    """
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    except (OSError, IOError):
        return
    try:
        total = 0
        while True:
            try:
                buf = os.read(fd, 65536)
                if not buf:
                    break
                total += len(buf)
            except BlockingIOError:
                break
        if total > 0:
            print(f"[drain] Cleared {total} bytes of stale data from FIFO", file=sys.stderr)
    finally:
        os.close(fd)


class MJPEGHandler(http.server.BaseHTTPRequestHandler):
    """HTTP 请求处理器：根路径返回 HTML，/video 返回 MJPEG 流"""

    def log_message(self, format, *args):
        # 精简日志，只打印关键信息
        msg = format % args
        if 'video' in msg or 'GET' not in msg:
            return
        print(f"[HTTP] {msg.strip()}", file=sys.stderr)

    def do_GET(self):
        if self.path == '/':
            # 返回带视频画面的 HTML 页面
            self.send_response(200)
            self.send_header('Content-type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write('''<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Live Video Stream</title>
<style>
body { margin: 0; background: #111; color: #fff; font-family: sans-serif; text-align: center; }
h1 { margin: 20px 0 10px; }
.info { color: #888; font-size: 14px; margin-bottom: 20px; }
img { max-width: 95%; height: auto; border: 2px solid #333; border-radius: 4px; }
</style>
</head>
<body>
<h1>Live Video Stream</h1>
<div class="info">H.264 realtime stream | Browser native playback, no plugin needed</div>
<img src="/video" alt="live video">
</body>
</html>'''.encode('utf-8'))
            return

        if self.path != '/video':
            self.send_error(404)
            return

        # 清空 FIFO 中积累的旧数据，确保从当前帧开始播放
        # 避免用户打开浏览器较晚时，ffmpeg 先处理几秒钟的旧数据
        drain_fifo(FIFO_PATH)

        # 启动 ffmpeg 子进程，读取 H.264 输出 MJPEG
        cmd = [
            'ffmpeg',
            '-fflags', 'nobuffer',
            '-flags', 'low_delay',
            '-analyzeduration', '0',
            '-probesize', '32',
            '-f', 'h264',
            '-i', FIFO_PATH,
            '-r', '25',       # 限制输出帧率为 25fps，避免播放过快
            '-f', 'mjpeg',
            '-q:v', '5',      # JPEG 质量 1-31，越小越好
            'pipe:1'
        ]

        print(f"[ffmpeg] 启动: {' '.join(cmd)}", file=sys.stderr)
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # 后台线程打印 ffmpeg 的调试信息
        def log_stderr():
            for line in proc.stderr:
                s = line.decode('utf-8', errors='replace').rstrip()
                if s and ('error' in s.lower() or 'frame=' in s or 'Input' in s):
                    print(f"[ffmpeg] {s}", file=sys.stderr)

        import threading
        threading.Thread(target=log_stderr, daemon=True).start()

        # 发送 HTTP 响应头
        self.send_response(200)
        self.send_header('Content-type', 'multipart/x-mixed-replace; boundary=frame')
        self.send_header('Cache-Control', 'no-cache')
        self.end_headers()

        # 从 ffmpeg stdout 读取 MJPEG 数据，分割 JPEG 帧
        buffer = b''
        frame_count = 0
        boundary = b'--frame\r\nContent-Type: image/jpeg\r\n\r\n'

        while True:
            chunk = proc.stdout.read(65536)
            if not chunk:
                print("[mjpeg] ffmpeg 输出结束，等待重连...", file=sys.stderr)
                proc.wait()
                # 重新启动 ffmpeg
                proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                threading.Thread(target=log_stderr, daemon=True).start()
                buffer = b''
                continue

            buffer += chunk

            # 分割 JPEG 帧: 以 FF D8 开头，FF D9 结尾
            while True:
                soi = buffer.find(b'\xff\xd8')
                if soi == -1:
                    buffer = buffer[-10:] if len(buffer) > 10 else buffer
                    break

                eoi = buffer.find(b'\xff\xd9', soi)
                if eoi == -1:
                    buffer = buffer[soi:]
                    break

                jpeg = buffer[soi:eoi + 2]
                buffer = buffer[eoi + 2:]
                frame_count += 1

                if frame_count % 30 == 0:
                    print(f"[mjpeg] 已推送 {frame_count} 帧", file=sys.stderr)

                try:
                    self.wfile.write(boundary + jpeg + b'\r\n')
                    self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    print("[mjpeg] 客户端断开", file=sys.stderr)
                    proc.terminate()
                    return


class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    """支持多线程的 HTTP 服务器，允许多个客户端同时观看"""
    allow_reuse_address = True
    daemon_threads = True


if __name__ == '__main__':
    print("=" * 60)
    print("  极简 H.264 实时视频 HTTP 服务器")
    print("=" * 60)
    print(f"  FIFO 输入: {FIFO_PATH}")
    print(f"  HTTP 地址: http://0.0.0.0:{PORT}")
    print(f"  视频流:   http://0.0.0.0:{PORT}/video")
    print("=" * 60)
    print("\n请先确保:")
    print("  1. 接收端已启动 (创建 /tmp/video_live.h264 管道)")
    print("  2. 发送端正在发送视频数据")
    print("\n然后用浏览器打开上述地址即可观看。\n")

    try:
        with ThreadedHTTPServer(('', PORT), MJPEGHandler) as httpd:
            httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n服务器已停止")
