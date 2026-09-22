#!/usr/bin/env bash
# 为 mingw 交叉编译 QScintilla 静态库 (供 stock_gui 的策略代码编辑器使用)
#
# 用法: ./scripts/build_qscintilla_static.sh [QScintilla版本] [安装前缀]
#
# 背景 / 为什么需要它
#   "策略代码工作室"原先用 QTextEdit + 自写的 LuaHighlighter, 只能做语法着色,
#   缺行号栏/括号匹配/自动缩进/折叠。改用 QScintilla(Scintilla 的 Qt 封装),
#   它有官方 Lua 词法器与完整代码编辑能力。
#
#   ⚠ 许可证: QScintilla 是 GPL v3 (或向 Riverbank 购买商业授权)。
#     本项目为内部使用、不对外分发, 故采用 GPL v3 版本。
#
# 前置条件
#   /home/srdev/devdisk/qt-static 已就绪(见 build_static_qt_tls.sh)。
#   该静态 Qt 的 bin/qmake、moc 都是 Linux 宿主可执行文件, 且
#   QMAKE_XSPEC=win32-g++, 因此直接在 Linux 上跑就能产出 mingw 目标文件。
set -euo pipefail

VER="${1:-2.14.1}"
DST="${2:-/home/srdev/devdisk/qscintilla-install}"
WORK=/home/srdev/devdisk/qscintilla
TOOLCHAIN=/home/srdev/devdisk/work/cc-toolchain
QT=/home/srdev/devdisk/qt-static

export PATH="$TOOLCHAIN/bin:$TOOLCHAIN/mingw64/usr/bin:/usr/bin:/bin"

mkdir -p "$WORK"
cd "$WORK"
if [ ! -d "QScintilla-$VER" ]; then
    echo "[qscintilla] 下载 QScintilla-$VER 源码"
    URL=$(curl -sS -m 30 https://pypi.org/pypi/QScintilla/json \
          | python3 -c "import json,sys; d=json.load(sys.stdin); print([f['url'] for f in d['urls'] if f['packagetype']=='sdist'][0])")
    curl -sSL -m 300 -o "QScintilla-$VER.tar.gz" "$URL"
    tar xzf "QScintilla-$VER.tar.gz"
fi

cd "QScintilla-$VER/src"
echo "[qscintilla] qmake (交叉: win32-g++ / 静态库)"
"$QT/bin/qmake" qscintilla.pro CONFIG+=staticlib CONFIG+=release
echo "[qscintilla] make"
make -j"$(nproc)"

echo "[qscintilla] 安装到 $DST"
rm -rf "$DST"
mkdir -p "$DST/include/Qsci" "$DST/lib"
cp Qsci/*.h "$DST/include/Qsci/"
cp release/libqscintilla2_qt5.a "$DST/lib/"

echo "[qscintilla] 校验"
ls -la "$DST/lib/libqscintilla2_qt5.a"
ar t "$DST/lib/libqscintilla2_qt5.a" | grep -i lua
echo "[qscintilla] DONE -> $DST"
