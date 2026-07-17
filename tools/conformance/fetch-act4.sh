#!/bin/sh
# 文件职责：从官方仓库获取固定 ACT4 4.0.0，并验证其精确提交而非移动分支。
# 边界：脚本只写入已忽略的 artifacts/downloads，不安装系统包、不修改测试源码。

set -eu

ACT4_DIR="artifacts/downloads/riscv-arch-test"
ACT4_TAG="4.0.0"
ACT4_COMMIT="a7c99303516f4e668f7488f172043392e23b9dfd"

if [ ! -f "CMakeLists.txt" ] || [ ! -d ".git" ]; then
    echo "错误：请从仓库根目录运行本脚本" >&2
    exit 2
fi

if [ ! -d "$ACT4_DIR/.git" ]; then
    mkdir -p "artifacts/downloads"
    git clone --branch "$ACT4_TAG" --depth 1 \
        "https://github.com/riscv/riscv-arch-test.git" "$ACT4_DIR"
fi

actual_commit=$(git -C "$ACT4_DIR" rev-parse HEAD)
if [ "$actual_commit" != "$ACT4_COMMIT" ]; then
    echo "错误：ACT4 提交不匹配，期望 $ACT4_COMMIT，实际 $actual_commit" >&2
    exit 1
fi

echo "ACT4 $ACT4_TAG 已验证：$actual_commit"
