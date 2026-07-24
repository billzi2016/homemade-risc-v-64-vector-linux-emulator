#!/usr/bin/env bash
# 文件职责：负责在 docs-site 目录下执行 Markdown symlink 规范检查及 mkdocs --strict 模式站点构建。
# 边界：不修改宿主网络、不上推 GitHub Pages，仅负责本地文档站的严格编译验证。
# 主要依赖：python3, mkdocs (含 mkdocs-material / mkdocs-static-i18n 插件), check_docs.py 检查脚本。
# 关键不变量：必须带 --strict 开关运行构建，任何链接断裂、缺少字段或配置语法错误均必须立刻退出报错。

set -euo pipefail

# 确保脚本工作目录为 docs-site 根目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

echo "=================================================="
echo " 1. 运行文档入口与 symlink 规范检查..."
echo "=================================================="
python scripts/check_docs.py

echo ""
echo "=================================================="
echo " 2. 检查 mkdocs 环境..."
echo "=================================================="

# 尝试激活可能存在的 Python 虚拟环境
VENV_PATHS=(
  "${SCRIPT_DIR}/.venv/bin/activate"
  "${SCRIPT_DIR}/../artifacts/downloads/riscv-arch-test/.venv/bin/activate"
)

for venv_act in "${VENV_PATHS[@]}"; do
  if [[ -f "${venv_act}" ]]; then
    echo "[提示] 激活虚拟环境: ${venv_act}"
    # shellcheck disable=SC1090
    source "${venv_act}"
    break
  fi
done

# 确认 mkdocs 可用
if ! command -v mkdocs &>/dev/null; then
  echo "[错误] 未找到 mkdocs 命令。请确保已安装 mkdocs 及依赖插件 (如 mkdocs-material, mkdocs-static-i18n)。" >&2
  exit 1
fi

echo "[信息] 使用 mkdocs 路径: $(command -v mkdocs)"

echo ""
echo "=================================================="
echo " 3. 执行 mkdocs build --strict 编译站点..."
echo "=================================================="
mkdocs build --strict

echo ""
echo "=================================================="
echo "[成功] MkDocs 双语站点 Strict 模式构建成功！"
echo "产物路径: ${SCRIPT_DIR}/site"
echo "=================================================="
