# cross-cc-toolchain-x64.cmake
# Linux -> Windows x86-64 交叉编译工具链 —— 匹配本机 cc-toolchain/qtwin (Qt5.15, 动态 Qt)
#
# 用法 (在项目根目录):
#   cmake -DCMAKE_TOOLCHAIN_FILE=../cross-cc-toolchain-x64.cmake ..
#   make -j$(nproc)
#
# 产物: 链接 Qt5*.dll 的可执行文件; 部署时须附带 bin 下的 Qt DLL 与 platforms/qwindows.dll
# 静态链接 gcc/libstdc++ 运行时由工程 CMakeLists.txt 的 MINGW 分支负责 (-static-libgcc -static-libstdc++)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# 编译器: 使用 PATH 中的 x86_64-w64-mingw32 系列交叉工具
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# 本机 Qt5 安装位置 (由 cc-toolchain/qtwin 安装)
set(QT_INSTALL_PREFIX /home/srdev/devdisk/work/cc-toolchain/qtwin)
set(CMAKE_FIND_ROOT_PATH ${QT_INSTALL_PREFIX})

# 交叉查找策略: 程序在主机上找, 库/头文件只在目标根下找
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 让 find_package(Qt5 ...) 定位到 qtwin
set(CMAKE_PREFIX_PATH ${QT_INSTALL_PREFIX})
