#!/usr/bin/env bash
# 打包可部署的 Windows x86-64 软件包 (Qt5 动态, 附带全部运行期 DLL)
# 用法:
#   ./scripts/deploy_win.sh [BUILD_DIR] [ZIP_BASENAME]
#     BUILD_DIR        编译输出目录, 默认 build_release_x64
#     ZIP_BASENAME     输出 zip 前缀, 默认 stock_gui-win64 (CI 会带 commit+时间戳)
set -euo pipefail

BUILD_DIR="${1:-build_release_x64}"
QTWIN="/home/srdev/devdisk/work/cc-toolchain/qtwin"
ZIP_BASENAME="${2:-stock_gui-win64}"

if [ ! -f "$BUILD_DIR/stock_gui.exe" ]; then
  echo "[deploy] ERROR: 未找到 $BUILD_DIR/stock_gui.exe, 请先 ./build.sh" >&2
  exit 1
fi

DEST="${ZIP_BASENAME%-zip}"   # 解压后文件夹名, 去掉末尾 -zip (若有)
[ "$DEST" = "$ZIP_BASENAME" ] && DEST="stock_gui-win64"
rm -rf "$DEST"; mkdir -p "$DEST/platforms"

echo "[deploy] 复制可执行文件"
cp "$BUILD_DIR/stock_gui.exe" "$BUILD_DIR/engine_test.exe" "$BUILD_DIR/bt_test.exe" "$DEST/" 2>/dev/null || true

echo "[deploy] 复制 Qt5 运行期 DLL (exe 实际引用)"
for d in Qt5Core Qt5Gui Qt5Network Qt5Widgets; do
  if [ -f "$QTWIN/bin/$d.dll" ]; then cp "$QTWIN/bin/$d.dll" "$DEST/"; else echo "[deploy] WARN: 缺失 $d.dll"; fi
done

echo "[deploy] 复制 Qt 平台插件 (静态嵌入, 附带无妨)"
cp "$QTWIN/plugins/platforms/qwindows.dll" "$DEST/platforms/" 2>/dev/null || true

cat > "$DEST/start.bat" <<'BAT'
@echo off
title 股票体检 stock_gui
echo 正在启动 stock_gui ...
start "" stock_gui.exe
BAT

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

echo "[deploy] 打包"
zip -r -q "$ZIP_BASENAME.zip" "$DEST"
rm -rf "$DEST"

echo "[deploy] DONE -> $ZIP_BASENAME.zip"
ls -la "$ZIP_BASENAME.zip"
