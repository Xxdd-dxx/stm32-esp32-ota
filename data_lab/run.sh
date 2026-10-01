#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# data_lab 练习运行脚本
#
#   ./run.sh 1            编译并运行练习 1
#   ./run.sh 1 ub         练习 1 + UBSan（检查未定义行为）
#   ./run.sh 6 asan       练习 6 + AddressSanitizer（检查越界）
#   ./run.sh all          依次跑 1~8，打印每个的通过/失败
#   ./run.sh clean        清掉所有生成的可执行文件
#
# 编译参数统一用 -Wall -Wextra，警告一个都不放过 —— 这正是你在 Keil 里
# 应该养成的习惯：警告往往就是"这里想错了"的早期信号。
# ---------------------------------------------------------------------------

set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR" || exit 1

CC="${CC:-gcc}"
CFLAGS="-Wall -Wextra -I ."

# ---------- 找出练习文件 ----------
find_ex() {
    local n
    n=$(printf "%02d" "$1")
    local f
    f=$(ls ex"$n"_*.c 2>/dev/null | head -1)
    echo "$f"
}

# ---------- 编译并运行单个练习 ----------
run_one() {
    local num="$1"
    local mode="${2:-}"

    local src
    src=$(find_ex "$num")
    if [ -z "$src" ]; then
        echo "找不到练习 $num（期望 ex$(printf "%02d" "$num")_*.c）"
        return 2
    fi

    local bin="_run_ex$(printf "%02d" "$num")"
    local extra=""

    case "$mode" in
        ub)   extra="-fsanitize=undefined -fno-sanitize-recover=all -g" ;;
        asan) extra="-fsanitize=address -g" ;;
        "")   extra="-O2" ;;
        *)    extra="$mode" ;;
    esac

    echo "=============================================================="
    echo "  $src   编译参数: $CFLAGS $extra"
    echo "=============================================================="

    # shellcheck disable=SC2086
    if ! $CC $CFLAGS $extra -o "$bin" "$src"; then
        echo ">>> 编译失败：先解决编译错误（即使只是警告也建议看一下）"
        return 3
    fi

    ./"$bin"
    local rc=$?
    echo "---- 退出码 $rc ----"
    rm -f "$bin" "$bin.exe"
    return $rc
}

# ---------- 主入口 ----------
case "${1:-}" in
    ""|-h|--help)
        sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    clean)
        rm -f _run_ex* _chk_* _t_* _tmp_*
        echo "已清理生成的可执行文件"
        exit 0
        ;;
    all)
        for i in 1 2 3 4 5 6 7 8; do
            if run_one "$i" >/dev/null 2>&1; then
                echo "  [PASS] 练习 $i"
            else
                echo "  [FAIL] 练习 $i   （源码: $(find_ex "$i")）"
            fi
        done
        echo
        echo "=============================================================="
        echo "  没填 TODO 的文件会显示 FAIL，这是预期的 —— 去把 TODO 填掉。"
        echo "  想看某个练习的详细输出： ./run.sh <编号>"
        echo "=============================================================="
        ;;
    *)
        num="$1"
        mode="${2:-}"
        run_one "$num" "$mode"
        ;;
esac
