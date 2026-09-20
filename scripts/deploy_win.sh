#!/usr/bin/env bash
# 打包可部署的 Windows x86-64 软件包
#   动态 Qt (默认): 附带全部 Qt5 运行期 DLL + platforms/qwindows.dll
#   静态 Qt (mode=static): 产物为单文件零依赖 exe, 不附带任何 Qt DLL
# 用法:
#   ./scripts/deploy_win.sh [BUILD_DIR] [ZIP_BASENAME] [MODE]
#     BUILD_DIR     编译输出目录, 默认 build_release_x64
#     ZIP_BASENAME  输出 zip 前缀, 默认 stock_gui-win64 (CI 会带 commit+时间戳)
#     MODE        auto|dynamic|static, 默认 auto (按 BUILD_DIR 名自动判定)
set -euo pipefail

# 仓库根目录 (deploy_win.sh 位于 scripts/ 下, 取父目录)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILD_DIR="${1:-build_release_x64}"
QTWIN="/home/srdev/devdisk/work/cc-toolchain/qtwin"
ZIP_BASENAME="${2:-stock_gui-win64}"
MODE="${3:-auto}"
case "$MODE" in
  auto) case "${BUILD_DIR#*/}" in *static*) MODE=static;; *) MODE=dynamic;; esac ;;
esac
if [ ! -f "$BUILD_DIR/stock_gui.exe" ]; then
  echo "[deploy] ERROR: 未找到 $BUILD_DIR/stock_gui.exe, 请先 ./build.sh" >&2
  exit 1
fi

DEST="${ZIP_BASENAME%-zip}"   # 解压后文件夹名, 去掉末尾 -zip (若有)
[ "$DEST" = "$ZIP_BASENAME" ] && DEST="stock_gui-win64"
rm -rf "$DEST"
if [ "$MODE" = "static" ]; then
  mkdir -p "$DEST"
else
  mkdir -p "$DEST/platforms"
fi

echo "[deploy] 复制可执行文件"
cp "$BUILD_DIR/stock_gui.exe" "$BUILD_DIR/engine_test.exe" "$BUILD_DIR/bt_test.exe" "$DEST/" 2>/dev/null || true

if [ "$MODE" = "static" ]; then
  echo "[deploy] 静态构建: 不附带 Qt DLL / 平台插件 (已静态嵌入 exe)"
else
  echo "[deploy] 复制 Qt5 运行期 DLL (exe 实际引用)"
  for d in Qt5Core Qt5Gui Qt5Network Qt5Widgets; do
    if [ -f "$QTWIN/bin/$d.dll" ]; then cp "$QTWIN/bin/$d.dll" "$DEST/"; else echo "[deploy] WARN: 缺失 $d.dll"; fi
  done
  echo "[deploy] 复制 Qt 平台插件 (若存在)"
  cp "$QTWIN/plugins/platforms/qwindows.dll" "$DEST/platforms/" 2>/dev/null || true
fi

# 静态构建: 附带应用数据文件 (策略脚本 + 映射表 + 数据库 + 使用说明)
#   策略脚本: 来自仓库 scripts/strategies (source of truth, CI 保证存在)
#   数据库/映射表/说明: 来自数据源目录 DEPLOY_DATA_DIR (默认旧发布包所在目录)
if [ "$MODE" = "static" ]; then
  DATA_DIR="${DEPLOY_DATA_DIR:-/home/srdev/devdisk/work/stock_gui_win32/stock_gui_win32}"
  echo "[deploy] 复制策略脚本 (来自仓库 $SCRIPT_DIR/strategies)"
  if [ -d "$SCRIPT_DIR/strategies" ]; then
    cp -r "$SCRIPT_DIR/strategies" "$DEST/" 2>/dev/null || true
  else
    echo "[deploy] WARN: 仓库无 strategies 目录" >&2
  fi
  if [ -d "$DATA_DIR" ]; then
    echo "[deploy] 复制数据库/映射表/说明 (来自 $DATA_DIR)"
    for f in concept_map.csv industry_map.csv kline_latest.db valuation.db "使用说明.txt"; do
      cp "$DATA_DIR/$f" "$DEST/" 2>/dev/null || echo "[deploy] WARN: 缺失 $f"
    done
  else
    echo "[deploy] WARN: 数据目录 $DATA_DIR 不存在, 跳过数据库/映射表/说明" >&2
  fi
fi

cat > "$DEST/start.bat" <<'BAT'
@echo off
title 股票体检 stock_gui
echo 正在启动 stock_gui ...
start "" stock_gui.exe
BAT

if [ "$MODE" = "static" ]; then
cat > "$DEST/README.txt" <<'TXT'
股票体检工具 (stock_gui) - Windows x86-64 部署包 (静态构建)
----------------------------------------------------------
运行方法: 双击 start.bat, 或直接双击 stock_gui.exe。

包含文件:
  stock_gui.exe        GUI 主程序 (静态 Qt, 单文件零依赖)
  engine_test.exe      引擎命令行工具
  bt_test.exe          策略回测工具

说明: 已把整个 Qt5 (Core/Gui/Widgets/Network)、Qt 平台插件及运行期库
      全部静态编译进 exe, 无需附带任何 DLL。复制到其它 Windows 电脑
      解包即可运行。

附带数据: strategies/ (策略脚本)、concept_map.csv / industry_map.csv
          (行业/概念映射)、kline_latest.db / valuation.db (行情/估值库),
          解包即可直接使用。
TXT
else
cat > "$DEST/README.txt" <<'TXT'
股票体检工具 (stock_gui) - Windows x86-64 部署包
------------------------------------------------
运行方法: 双击 start.bat, 或直接双击 stock_gui.exe。

包含文件:
  stock_gui.exe        GUI 主程序
  engine_test.exe      引擎命令行工具
  bt_test.exe          策略回测工具
  Qt5Core/Gui/Network/Widgets.dll   Qt5 运行期库(已附带)
  platforms/qwindows.dll            Qt5 平台插件

说明: 已附带全部运行期依赖, 可直接复制到其它 Windows 电脑解包运行。
TXT
fi

echo "[deploy] 打包 (max 压缩)"
zip -r -q -9 "$ZIP_BASENAME.zip" "$DEST"
rm -rf "$DEST"

echo "[deploy] DONE -> $ZIP_BASENAME.zip"
ls -la "$ZIP_BASENAME.zip"
