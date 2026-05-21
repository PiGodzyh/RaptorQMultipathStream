#!/bin/bash
# 带宽阶梯测试：不同 tc 限速下 FC 与 Video 的传输效率对比
# 输出：终端表格 + data/bw_ladder_result.csv

set -e

cd ~/code/RaptorQMultipathStream

# 创建结果目录
mkdir -p data
RESULT_CSV="data/bw_ladder_result.csv"
echo "bandwidth,fc_sent,fc_recv,fc_arrival_pct,fc_delay_ms,video_sent,video_recv,video_arrival_pct,video_jitter_ms,video_pred_bw_kbps" > $RESULT_CSV

RATES=("2mbit" "3mbit" "5mbit" "10mbit" "clean")

echo "========================================"
echo "  带宽阶梯测试（Feedback 预测模式）"
echo "========================================"
echo ""

for RATE in "${RATES[@]}"; do
    echo "--- [$RATE] ---"
    
    # 1. 设置 tc
    sudo tc qdisc del dev lo root 2>/dev/null || true
    if [ "$RATE" != "clean" ]; then
        sudo tc qdisc add dev lo root netem rate $RATE
        echo "  tc: rate $RATE"
    else
        echo "  tc: 无限速"
    fi
    
    # 2. 清理日志
    rm -f logs/sender.log logs/fc_tx.log logs/video_tx.log logs/video_rx.log output/fc/received.log
    
    # 3. 启动接收端
    ./build/raptorq_demo receiver > logs/receiver.log 2>&1 &
    RX_PID=$!
    sleep 2
    
    # 4. 启动发送端（30秒，自动发300条FC）
    (
        for i in $(seq 1 300); do
            echo "CMD_$i"
            sleep 0.1
        done
        echo "quit"
    ) | ./build/raptorq_demo sender 127.0.0.1 fc,video > logs/sender.log 2>&1 &
    TX_PID=$!
    
    # 5. 运行 30 秒
    sleep 30
    
    # 6. 停止
    kill -INT $TX_PID 2>/dev/null || true
    kill -INT $RX_PID 2>/dev/null || true
    sleep 3
    kill -9 $TX_PID 2>/dev/null || true
    kill -9 $RX_PID 2>/dev/null || true
    
    # 7. 分析数据
    FC_SENT=$(wc -l < logs/fc_tx.log 2>/dev/null || echo 0)
    FC_RECV=$(wc -l < output/fc/received.log 2>/dev/null || echo 0)
    
    if [ "$FC_SENT" -gt 0 ]; then
        FC_ARRIVAL=$(echo "scale=1; $FC_RECV * 100 / $FC_SENT" | bc)
    else
        FC_ARRIVAL="0.0"
    fi
    
    FC_DELAY=$(awk -F',' '{sum+=$4; n++} END {if(n>0) printf "%.2f", sum/n; else print "N/A"}' output/fc/received.log 2>/dev/null || echo "N/A")
    
    VIDEO_SENT=$(wc -l < logs/video_tx.log 2>/dev/null || echo 0)
    VIDEO_RECV=$(wc -l < logs/video_rx.log 2>/dev/null || echo 0)
    
    if [ "$VIDEO_SENT" -gt 0 ]; then
        VIDEO_ARRIVAL=$(echo "scale=1; $VIDEO_RECV * 100 / $VIDEO_SENT" | bc)
    else
        VIDEO_ARRIVAL="0.0"
    fi
    
    # Video 帧间隔抖动（标准差，毫秒）
    VIDEO_JITTER=$(awk -F',' '
        NR>1 {
            d=$1-prev;
            sum+=d; sq+=d*d; n++
        }
        {prev=$1}
        END {
            if(n>0) {
                mean=sum/n;
                var=sq/n-mean*mean;
                if(var<0) var=0;
                printf "%.2f", sqrt(var)/1000
            } else {
                print "N/A"
            }
        }' logs/video_rx.log 2>/dev/null || echo "N/A")
    
    # Video 预测带宽（最后 5 个值的均值）
    VIDEO_PRED_BW=$(grep "FeedbackController.*Adjusted Video" logs/sender.log 2>/dev/null | grep -oP 'rate=\K[0-9]+' | tail -5 | awk '{sum+=$1; n++} END {if(n>0) printf "%.0f", sum/n; else print "N/A"}')
    
    # 8. 输出结果
    printf "  FC:   %3s sent / %3s recv (%5s%%), delay=%sms\n" "$FC_SENT" "$FC_RECV" "$FC_ARRIVAL" "$FC_DELAY"
    printf "  Video:%3s sent / %3s recv (%5s%%), jitter=%sms, pred_bw=%skbps\n" "$VIDEO_SENT" "$VIDEO_RECV" "$VIDEO_ARRIVAL" "$VIDEO_JITTER" "$VIDEO_PRED_BW"
    echo ""
    
    # 写入 CSV
    echo "$RATE,$FC_SENT,$FC_RECV,$FC_ARRIVAL,$FC_DELAY,$VIDEO_SENT,$VIDEO_RECV,$VIDEO_ARRIVAL,$VIDEO_JITTER,$VIDEO_PRED_BW" >> $RESULT_CSV
    
    # 保留每轮日志（可选，占空间）
    # cp logs/sender.log "logs/sender_$RATE.log"
    # cp logs/video_rx.log "logs/video_rx_$RATE.log"
    # cp output/fc/received.log "output/fc/received_$RATE.log"
done

# 清理 tc
sudo tc qdisc del dev lo root 2>/dev/null || true

echo "========================================"
echo "  测试完成"
echo "  结果保存到: $RESULT_CSV"
echo "========================================"
cat $RESULT_CSV
echo ""
