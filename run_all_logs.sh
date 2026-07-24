#!/usr/bin/env bash
# 文件职责：从仓库根目录一键刷新固定日志文件，覆盖构建、CTest 和真实 UART 启动记录。
# 边界：脚本只写 artifacts/logs/ 下的固定 .log 文件，不创建随机日志名，不修改宿主网络配置。

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ROOT_DIR
cd "$ROOT_DIR"

LOG_DIR="artifacts/logs"
BUILD_LOG="$LOG_DIR/build.log"
CTEST_LOG="$LOG_DIR/ctest.log"
UART_LOG="$LOG_DIR/linux-boot-uart.log"
DEBUG_UART_LOG="$LOG_DIR/linux-boot-uart-debug.log"

JOBS="${JOBS:-8}"
BOOT_SECONDS="${BOOT_SECONDS:-600}"
RUN_DEBUG_BOOT="${RUN_DEBUG_BOOT:-0}"

mkdir -p "$LOG_DIR"

run_logged() {
    local log_path="$1"
    shift
    {
        printf '[run_all_logs] command:'
        printf ' %q' "$@"
        printf '\n'
        "$@"
    } >"$log_path" 2>&1
    sanitize_log "$log_path"
}

sanitize_log() {
    local log_path="$1"
    perl -0pi -e 's/\Q$ENV{ROOT_DIR}\E\///g; s/\Q$ENV{ROOT_DIR}\E/./g' "$log_path"
}

run_with_timeout() {
    if command -v gtimeout >/dev/null 2>&1; then
        gtimeout --foreground "$BOOT_SECONDS" "$@"
    elif command -v timeout >/dev/null 2>&1; then
        timeout --foreground "$BOOT_SECONDS" "$@"
    else
        perl -e '
            my $seconds = shift @ARGV;
            my $pid = fork();
            die "fork failed\n" unless defined $pid;
            if ($pid == 0) {
                exec @ARGV;
                die "exec failed: $!\n";
            }
            local $SIG{ALRM} = sub {
                kill "INT", $pid;
                sleep 1;
                kill "TERM", $pid;
            };
            alarm $seconds;
            waitpid($pid, 0);
            my $status = $?;
            exit(($status & 127) ? (($status & 127) + 128) : ($status >> 8));
        ' "$BOOT_SECONDS" "$@"
    fi
}

run_uart_log() {
    local log_path="$1"
    local trace_enabled="$2"
    : >"$log_path"

    local -a command=(
        ./build/riscv_vector_emulator
        --bios artifacts/firmware/fw_jump.bin
        --kernel artifacts/kernel/Image
        --disk artifacts/disk/rootfs.ext4
        --net none
    )

    local boot_status=0
    if [[ "$trace_enabled" == "1" ]]; then
        {
            printf '[run_all_logs] boot command:'
            printf ' %q' "${command[@]}"
            printf '\n[run_all_logs] RVEMU_BOOT_TRACE=1\n'
            run_with_timeout env RVEMU_BOOT_TRACE=1 script -q /dev/null "${command[@]}"
        } >>"$log_path" 2>&1 || boot_status=$?
    else
        {
            printf '[run_all_logs] boot command:'
            printf ' %q' "${command[@]}"
            printf '\n'
            run_with_timeout script -q /dev/null "${command[@]}"
        } >>"$log_path" 2>&1 || boot_status=$?
    fi

    {
        printf '\n[run_all_logs] BOOT_SECONDS=%s\n' "$BOOT_SECONDS"
        printf '[run_all_logs] exit_status=%s\n' "$boot_status"
    } >>"$log_path"
    sanitize_log "$log_path"

    if [[ "$boot_status" != "0" && "$boot_status" != "124" && "$boot_status" != "130"
        && "$boot_status" != "142" && "$boot_status" != "143" ]]; then
        return "$boot_status"
    fi
}

printf '[run_all_logs] writing %s\n' "$BUILD_LOG"
run_logged "$BUILD_LOG" cmake --build build -j"$JOBS"

printf '[run_all_logs] writing %s\n' "$CTEST_LOG"
run_logged "$CTEST_LOG" ctest --test-dir build --output-on-failure

printf '[run_all_logs] writing %s\n' "$UART_LOG"
run_uart_log "$UART_LOG" 0

if [[ "$RUN_DEBUG_BOOT" == "1" ]]; then
    printf '[run_all_logs] writing %s\n' "$DEBUG_UART_LOG"
    run_uart_log "$DEBUG_UART_LOG" 1
fi

printf '[run_all_logs] done\n'
