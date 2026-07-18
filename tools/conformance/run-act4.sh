#!/bin/sh
# 文件职责：生成 ACT4 自校验 ELF，并逐个交给真实 rvemu CPU/Bus/RAM 执行后汇总结果。
# 边界：脚本不修改第三方测试、不替换参考结果，也不会把超时或失败计为通过。

set -eu

ACT4_DIR="artifacts/downloads/riscv-arch-test"
SAIL_BIN="artifacts/downloads/sail-riscv-source-0.10/build/c_emulator"
GEM_BIN="artifacts/tools/act4-gems/ruby/3.4.0/bin"
WORK_DIR="artifacts/act4-work"
LOG_DIR="artifacts/logs"
RUNNER="build/rvemu_conformance_runner"
CONFIG="tests/conformance/act4/test_config.yaml"
MAX_INSTRUCTIONS="${RVEMU_ACT4_MAX_INSTRUCTIONS:-20000000}"

if [ ! -x "$RUNNER" ]; then
    echo "错误：缺少 $RUNNER，请先完成项目构建" >&2
    exit 2
fi
if [ ! -x "$SAIL_BIN/sail_riscv_sim" ]; then
    echo "错误：缺少 Sail RISC-V 0.10，请按 docs/third-party.md 完成安装" >&2
    exit 2
fi

ruby_prefix=$(brew --prefix ruby@3.4)
z3_prefix=$(brew --prefix z3)
llvm_prefix=$(brew --prefix llvm@21)
lld_prefix=$(brew --prefix lld@21)
project_root=$(pwd)
export PATH="$project_root/$GEM_BIN:$ruby_prefix/bin:$llvm_prefix/bin:$lld_prefix/bin:$project_root/$SAIL_BIN:$PATH"
export BUNDLE_PATH="$project_root/artifacts/tools/act4-gems"
export GEM_HOME="$project_root/artifacts/tools/act4-gems/ruby/3.4.0"
export GEM_PATH="$GEM_HOME"
export XDG_CACHE_HOME="$project_root/artifacts/tools/cache"
export XDG_DATA_HOME="$project_root/artifacts/tools/data"

# UDB 0.1.9 的加载器支持 macOS，但其固定 Ruby z3 gem 仍按 libz3.so 名称请求动态库。
# 在项目缓存中只建立名称适配链接，实际加载的仍是 Homebrew 原生 Mach-O libz3.dylib。
z3_cache="$XDG_CACHE_HOME/udb/z3/z3-4.16.0/arm64"
mkdir -p "$z3_cache"
ln -sfn "$z3_prefix/lib/libz3.dylib" "$z3_cache/libz3.so"

mkdir -p "$WORK_DIR" "$LOG_DIR"

(cd "$ACT4_DIR" && \
    CONFIG_FILES="$project_root/$CONFIG" \
    WORKDIR="$project_root/$WORK_DIR" \
    make --jobs 8)

elf_dir="$WORK_DIR/rvemu-rv64imafdc/elfs"
result_log="$LOG_DIR/act4-rv64imafdc.log"
if [ ! -d "$elf_dir" ]; then
    echo "错误：ACT4 未生成预期 ELF 目录 $elf_dir" >&2
    exit 2
fi

: > "$result_log"
total=0
passed=0
failed=0
for elf in $(find "$elf_dir" -type f -name '*.elf' | sort); do
    total=$((total + 1))
    if "$RUNNER" "$elf" --max-instructions "$MAX_INSTRUCTIONS" >> "$result_log" 2>&1; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
done

echo "ACT4 总数=${total}，通过=${passed}，失败或超时=${failed}"
echo "完整日志：$result_log"
if [ "$total" -eq 0 ] || [ "$failed" -ne 0 ]; then
    exit 1
fi
