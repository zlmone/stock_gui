#!/usr/bin/env bash
# 重建"带 TLS 的静态 Qt5" (win32-g++ 交叉编译, Windows Schannel 后端)
#
# 背景 / 为什么需要本脚本
#   build.sh 的 USE_STATIC_QT=ON 依赖 /home/srdev/devdisk/qt-static 这套静态 Qt。
#   最初那套 Qt 是用下面这条命令配的, 既没开 OpenSSL 也没开 Schannel:
#       ./configure -release -static -no-opengl -platform linux-g++ \
#                   -xplatform win32-g++ -prefix <prefix> -confirm-license
#   结果产物 exe 完全没有 TLS 能力(只导入 WS2_32, 无 secur32/crypt32),
#   于是程序里所有 https:// 请求必然握手失败 -> 新闻源全空、腾讯前复权K线失败。
#
# 本脚本做的事
#   1. 用 -schannel 重新配置并静态编译 qtbase, 安装到 QT_PREFIX;
#      Schannel 是 Windows 原生 TLS, 只依赖系统自带的
#      Secur32/Crypt32/bcrypt/ncrypt, 因此 exe 依旧零外部依赖。
#   2. 修一个 mingw 跨编译的大小写坑:
#      Qt 的 ssl.pri 写的是 "-lSecur32" "-lCrypt32" (大写),
#      而 mingw sysroot 里只有 libsecur32.a / libcrypt32.a (小写),
#      在大小写敏感的 Linux 上 CMake 的 find_library 会报 "Library not found: Secur32"。
#      这里补两个别名软链接解决。
#
# 用法: ./scripts/build_static_qt_tls.sh [Qt源码目录] [安装前缀]
set -euo pipefail

QT_SRC="${1:-/home/srdev/devdisk/qt-static-src}"
QT_PREFIX="${2:-/home/srdev/devdisk/qt-static}"
TOOLCHAIN="/home/srdev/devdisk/work/cc-toolchain"
SYSROOT_LIB="$TOOLCHAIN/mingw64/usr/x86_64-w64-mingw32/lib"

export PATH="$TOOLCHAIN/bin:$TOOLCHAIN/mingw64/usr/bin:/usr/bin:/bin"

echo "[qt-tls] mingw sysroot 补 Secur32/Crypt32 大小写别名"
ln -sf libsecur32.a "$SYSROOT_LIB/libSecur32.a"
ln -sf libcrypt32.a "$SYSROOT_LIB/libCrypt32.a"

cd "$QT_SRC"
echo "[qt-tls] configure (-schannel, 无 OpenSSL)"
./configure -release -static -no-opengl \
  -platform linux-g++ -xplatform win32-g++ \
  -prefix "$QT_PREFIX" -confirm-license \
  -schannel -no-openssl \
  -nomake examples -nomake tests

grep -A3 'Qt Network:' config.summary | grep -E 'Schannel|OpenSSL' || true

echo "[qt-tls] 旧产物清理 + 编译 (避免残留对象导致静默不带 TLS)"
make clean
make -j"$(nproc)"
make install

echo "[qt-tls] 校验 Qt5Network.prl 是否带上 Schannel 系统库"
grep -o -- '-lSecur32.*' "$QT_PREFIX/lib/Qt5Network.prl" || {
  echo "[qt-tls] ERROR: Qt5Network.prl 未包含 -lSecur32, TLS 未生效" >&2; exit 1; }
echo "[qt-tls] DONE -> $QT_PREFIX"
