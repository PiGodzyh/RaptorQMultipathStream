#!/bin/bash
# setup_network.sh - 网络环境模拟（tc tbf + netem on loopback0）
#
# 用法:
#   sudo ./setup_network.sh loss 10              # 10% 丢包
#   sudo ./setup_network.sh delay 50 10          # 50ms 延迟 ±10ms 抖动
#   sudo ./setup_network.sh rate 10mbit          # 10Mbps 限速
#   sudo ./setup_network.sh loss_delay 10 50 10  # 丢包+延迟组合
#   sudo ./setup_network.sh rate_loss 10mbit 10  # 限速+丢包组合
#   sudo ./setup_network.sh rate_loss_delay 10mbit 10 50 10  # 三者组合
#   sudo ./setup_network.sh clean                # 清除规则
#   sudo ./setup_network.sh show                 # 查看当前规则

set -e

IFACE="loopback0"

show_usage() {
    echo "用法: sudo $0 <cmd> [args]"
    echo ""
    echo "命令:"
    echo "  loss <percent>                          设置丢包率 (0~100)"
    echo "  delay <ms> [jitter_ms]                  设置延迟和抖动"
    echo "  rate <bandwidth>                        限速 (如 10mbit, 1gbit)"
    echo "  loss_delay <loss%> <ms> [jitter]        丢包+延迟组合"
    echo "  rate_loss <bandwidth> <loss%>           限速+丢包组合"
    echo "  rate_delay <bandwidth> <ms> [jitter]    限速+延迟组合"
    echo "  rate_loss_delay <bw> <loss%> <ms> [jit] 限速+丢包+延迟组合"
    echo "  clean                                   清除所有规则"
    echo "  show                                    显示当前规则"
    echo ""
    echo "示例:"
    echo "  sudo $0 loss 10"
    echo "  sudo $0 delay 50 10"
    echo "  sudo $0 rate 10mbit"
    echo "  sudo $0 rate_loss_delay 10mbit 10 50 10"
}

check_root() {
    if [ "$EUID" -ne 0 ]; then
        echo "错误: 需要 root 权限，请使用 sudo"
        exit 1
    fi
}

cmd_clean() {
    tc qdisc del dev "$IFACE" root 2>/dev/null || true
    echo "已清除: ${IFACE} 所有 tc 规则"
}

# 检查 loopback0 是否存在
check_iface() {
    if ! ip link show "$IFACE" &>/dev/null; then
        echo "错误: 接口 ${IFACE} 不存在"
        echo "提示: 尝试使用 'lo' 或其他回环接口"
        exit 1
    fi
}

# 构建 netem 参数字符串
build_netem_args() {
    local loss="$1"
    local delay="$2"
    local jitter="${3:-0}"
    local args=""

    if [ -n "$loss" ] && [ "$loss" -gt 0 ]; then
        args="${args} loss ${loss}%"
    fi

    if [ -n "$delay" ] && [ "$delay" -gt 0 ]; then
        if [ "$jitter" -gt 0 ]; then
            args="${args} delay ${delay}ms ${jitter}ms distribution normal"
        else
            args="${args} delay ${delay}ms"
        fi
    fi

    echo "$args"
}

# 应用规则: tbf (root) -> netem (child)
apply_rules() {
    local rate="$1"
    local loss="$2"
    local delay="$3"
    local jitter="${4:-0}"

    cmd_clean

    # 1. root: tbf 限速
    if [ -n "$rate" ]; then
        tc qdisc add dev "$IFACE" root handle 1: tbf \
            rate "$rate" \
            burst 16k \
            limit 30k
        echo "已设置: ${IFACE} 限速 ${rate} (burst 16k, limit 30k)"
    fi

    # 2. child: netem 丢包/延迟
    local netem_args
    netem_args=$(build_netem_args "$loss" "$delay" "$jitter")

    if [ -n "$netem_args" ]; then
        if [ -n "$rate" ]; then
            # tbf 已存在，在 parent 1:1 下挂载 netem
            tc qdisc add dev "$IFACE" parent 1:1 handle 10: netem $netem_args
        else
            # 无 tbf，直接 root netem
            tc qdisc add dev "$IFACE" root handle 1: netem $netem_args
        fi
        echo "已设置: ${IFACE} netem ${netem_args}"
    fi

    echo ""
    echo "当前 ${IFACE} 规则:"
    tc qdisc show dev "$IFACE"
}

cmd_loss() {
    local loss="$1"
    if [ -z "$loss" ]; then
        echo "错误: 缺少丢包率参数"
        show_usage
        exit 1
    fi
    apply_rules "" "$loss" "" ""
}

cmd_delay() {
    local delay="$1"
    local jitter="${2:-0}"
    if [ -z "$delay" ]; then
        echo "错误: 缺少延迟参数"
        show_usage
        exit 1
    fi
    apply_rules "" "" "$delay" "$jitter"
}

cmd_rate() {
    local rate="$1"
    if [ -z "$rate" ]; then
        echo "错误: 缺少带宽参数"
        show_usage
        exit 1
    fi
    apply_rules "$rate" "" "" ""
}

cmd_loss_delay() {
    local loss="$1"
    local delay="$2"
    local jitter="${3:-0}"
    if [ -z "$loss" ] || [ -z "$delay" ]; then
        echo "错误: 缺少参数"
        show_usage
        exit 1
    fi
    apply_rules "" "$loss" "$delay" "$jitter"
}

cmd_rate_loss() {
    local rate="$1"
    local loss="$2"
    if [ -z "$rate" ] || [ -z "$loss" ]; then
        echo "错误: 缺少参数"
        show_usage
        exit 1
    fi
    apply_rules "$rate" "$loss" "" ""
}

cmd_rate_delay() {
    local rate="$1"
    local delay="$2"
    local jitter="${3:-0}"
    if [ -z "$rate" ] || [ -z "$delay" ]; then
        echo "错误: 缺少参数"
        show_usage
        exit 1
    fi
    apply_rules "$rate" "" "$delay" "$jitter"
}

cmd_rate_loss_delay() {
    local rate="$1"
    local loss="$2"
    local delay="$3"
    local jitter="${4:-0}"
    if [ -z "$rate" ] || [ -z "$loss" ] || [ -z "$delay" ]; then
        echo "错误: 缺少参数"
        show_usage
        exit 1
    fi
    apply_rules "$rate" "$loss" "$delay" "$jitter"
}

cmd_show() {
    echo "当前 ${IFACE} 规则:"
    tc qdisc show dev "$IFACE"
    echo ""
    echo "详细统计:"
    tc -s qdisc show dev "$IFACE"
}

# ============================================================
# 主入口
# ============================================================
check_root
check_iface

CMD="${1:-}"
case "$CMD" in
    loss)
        cmd_loss "$2"
        ;;
    delay)
        cmd_delay "$2" "$3"
        ;;
    rate)
        cmd_rate "$2"
        ;;
    loss_delay)
        cmd_loss_delay "$2" "$3" "$4"
        ;;
    rate_loss)
        cmd_rate_loss "$2" "$3"
        ;;
    rate_delay)
        cmd_rate_delay "$2" "$3" "$4"
        ;;
    rate_loss_delay)
        cmd_rate_loss_delay "$2" "$3" "$4" "$5"
        ;;
    clean)
        cmd_clean
        ;;
    show)
        cmd_show
        ;;
    *)
        show_usage
        exit 1
        ;;
esac
