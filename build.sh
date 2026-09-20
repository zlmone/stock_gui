#!/usr/bin/env bash
# 自动化交叉编译: Linux/WSL -> Windows x86-64 (Qt5 mingw)
# 用法:
#   ./build.sh                 # 动态 Qt release 构建 (产物附 Qt DLL 与 platforms/)
#   BUILD_TYPE=debug ./build.sh # 动态 Qt debug 构建
#   USE_STATIC_QT=ON ./build.sh # 静态 Qt 构建 -> 单文件零依赖 exe (约 38MB, 随包无需 Qt DLL)
#   ./build.sh -- -DFOO=bar     # 额外传给 cmake 的参数 (静态构建建议: -- -DQT_INSTALL_PREFIX=/path)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ---- 本机工具链环境 (本机已装 x86_64-w64-mingw32 与 qtwin) ----
export PATH="/home/srdev/devdisk/work/cc-toolchain/bin:/usr/bin:/bin:/home/srdev/devdisk/work/cc-toolchain/mingw64/usr/bin"
QTWIN="/home/srdev/devdisk/work/cc-toolchain/qtwin"
BUILD_TYPE="${BUILD_TYPE:-release}"

# ---- 定位 cmake (PATH, 其次常见本地安装位置) ----
CMAKE_BIN="$(command -v cmake || true)"
for cand in "$HOME/.local/bin/cmake" "$HOME/.local/cmake-3.31.6-linux-x86_64/bin/cmake"; do
  if [ -z "$CMAKE_BIN" ] && [ -x "$cand" ]; then
    CMAKE_BIN="$cand"; export PATH="$(dirname "$cand"):$PATH"
  fi
done
if [ -z "$CMAKE_BIN" ]; then
  echo "[build] ERROR: 未找到 cmake, 请安装并加入 PATH" >&2
  exit 1
fi
echo "[build] cmake : $CMAKE_BIN"
echo "[build] qtwin : $QTWIN"
echo "[build] type  : $BUILD_TYPE"

CMAKE_EXTRA_ARGS=()
if [ $# -gt 0 ] && [ "$1" = "--" ]; then shift; CMAKE_EXTRA_ARGS=("$@"); fi

# 静态 Qt 构建: 指定静态 Qt 安装目录并启用静态链接 (-static)
if [ "${USE_STATIC_QT:-OFF}" = "ON" ]; then
  QT_STATIC_PREFIX="${QT_STATIC_PREFIX:-/home/srdev/devdisk/qt-static}"
  CMAKE_EXTRA_ARGS+=(-DUSE_STATIC_QT=ON -DQT_INSTALL_PREFIX="$QT_STATIC_PREFIX")
fi

BUILD_DIR="build_${BUILD_TYPE}_x64"
if [ "${USE_STATIC_QT:-OFF}" = "ON" ]; then
  BUILD_DIR="build_static_x64"
fi
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "[build] configuring..."
"$CMAKE_BIN" -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cross-cc-toolchain-x64.cmake" \
  "${CMAKE_EXTRA_ARGS[@]}"

echo "[build] compiling (jobs=$(nproc || echo 4))..."
make -C "$BUILD_DIR" -j"$(nproc || echo 4)"

echo "[build] verifying PE32+ artifacts..."
FAIL=0
for exe in engine_test stock_gui; do
  if [ -f "$BUILD_DIR/$exe.exe" ]; then
    if file "$BUILD_DIR/$exe.exe" | grep -q "MS Windows"; then
      printf "  OK   : %s\n" "$(file -b "$BUILD_DIR/$exe.exe")"
    else
      echo "  FAIL: $exe.exe 不是 Windows 可执行文件"; FAIL=1
    fi
  else
    echo "  skip : $exe.exe 未构建 (可能缺 Qt5)"
  fi
done

echo "[build] sizes:"
ls -la "$BUILD_DIR"/*.exe 2>/dev/null || true

if [ "$FAIL" -ne 0 ]; then
  echo "[build] ERROR: 产物验证失败" >&2
  exit 1
fi
echo "[build] DONE -> $BUILD_DIR"
