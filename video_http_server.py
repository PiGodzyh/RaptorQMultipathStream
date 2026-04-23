#!/usr/bin/env python3
"""
H.264 实时视频 HTTP 流媒体服务器

通过 ffmpeg 读取 /tmp/video_live.h264 FIFO 管道，解码为 MJPEG，
通过 Flask HTTP 服务器以 multipart/x-mixed-replace 方式推流到浏览器。

用法:
    1. 先启动接收端: ./build/multi_streaming_demo receiver video 9001 output.mp4
    2. 启动本服务器: source .venv/bin/activate && python video_http_server.py
    3. 启动发送端: ./build/multi_streaming_demo sender video <ip> 9001 test.mp4
    4. 浏览器访问: http://<服务器IP>:8080

依赖:
    - Python 3 + Flask (已安装在 .venv 中)
    - ffmpeg
"""

import subprocess
import sys
from flask import Flask, Response

app = Flask(__name__)

FIFO_PATH = '/tmp/video_live.h264'


def generate_mjpeg():
    """用 ffmpeg 读取 H.264 FIFO，输出 MJPEG 流，分割成帧推送到浏览器。"""
    cmd = [
        'ffmpeg',
        '-fflags', 'nobuffer',
        '-flags', 'low_delay',
        '-analyzeduration', '0',
        '-probesize', '32',
        '-f', 'h264',
        '-i', FIFO_PATH,
        '-f', 'mjpeg',
        '-q:v', '5',           # JPEG 质量，1-31，越小越好
        'pipe:1'
    ]

    print(f"[ffmpeg] Starting: {' '.join(cmd)}", file=sys.stderr)
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    # 在另一个线程打印 ffmpeg stderr，便于调试
    def log_stderr():
        for line in proc.stderr:
            line_str = line.decode('utf-8', errors='replace').rstrip()
            if line_str:
                print(f"[ffmpeg] {line_str}", file=sys.stderr)

    import threading
    threading.Thread(target=log_stderr, daemon=True).start()

    buffer = b''
    frame_count = 0

    while True:
        chunk = proc.stdout.read(65536)
        if not chunk:
            print("[mjpeg] ffmpeg stdout closed, exiting", file=sys.stderr)
            break

        buffer += chunk

        # JPEG 帧以 FF D8 开始，FF D9 结束
        while True:
            soi = buffer.find(b'\xff\xd8')
            if soi == -1:
                # 保留最后几个字节，防止 FF D8 被截断
                buffer = buffer[-10:] if len(buffer) > 10 else buffer
                break

            eoi = buffer.find(b'\xff\xd9', soi)
            if eoi == -1:
                # 帧还没读完，保留从 SOI 开始的数据
                buffer = buffer[soi:]
                break

            jpeg = buffer[soi:eoi + 2]
            buffer = buffer[eoi + 2:]
            frame_count += 1

            if frame_count % 30 == 0:
                print(f"[mjpeg] Served {frame_count} frames", file=sys.stderr)

            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n'
                   b'Content-Length: ' + str(len(jpeg)).encode() + b'\r\n'
                   b'\r\n' + jpeg + b'\r\n')


@app.route('/video')
def video():
    return Response(
        generate_mjpeg(),
        mimetype='multipart/x-mixed-replace; boundary=frame'
    )


@app.route('/')
def index():
    return '''<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>实时视频流</title>
    <style>
        body { margin: 0; background: #111; color: #fff; font-family: sans-serif; }
        .container { text-align: center; padding: 20px; }
        h1 { margin-bottom: 10px; }
        .info { color: #888; font-size: 14px; margin-bottom: 20px; }
        img { max-width: 100%; height: auto; border: 2px solid #333; border-radius: 4px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎥 实时视频流</h1>
        <div class="info">H.264 → MJPEG HTTP Stream | 浏览器直接播放，无需插件</div>
        <img src="/video" alt="live video">
    </div>
</body>
</html>'''


if __name__ == '__main__':
    print("=" * 60)
    print("  H.264 实时视频 HTTP 流媒体服务器")
    print("=" * 60)
    print(f"  FIFO 输入: {FIFO_PATH}")
    print("  HTTP 地址: http://0.0.0.0:8080")
    print("  视频流地址: http://0.0.0.0:8080/video")
    print("=" * 60)
    print("\n请先确保:")
    print("  1. 接收端已启动 (创建 /tmp/video_live.h264 管道)")
    print("  2. 发送端正在发送视频数据")
    print("\n然后在浏览器中打开上述地址即可观看。\n")

    app.run(host='0.0.0.0', port=8080, threaded=True)
