# cross-cc-toolchain-x64.cmake
# Linux -> Windows x86-64 交叉编译工具链
#
# 匹配两种 Qt:
#   - 动态 Qt : cc-toolchain/qtwin        (默认, 产物链接 Qt5*.dll)
#   - 静态 Qt : 用 -DQT_INSTALL_PREFIX=<静态 Qt 安装目录> 指定, 产物为单文件零依赖 exe
#
# 用法:
#   cmake -DCMAKE_TOOLCHAIN_FILE=../cross-cc-toolchain-x64.cmake ..
#   cmake -DCMAKE_TOOLCHAIN_FILE=../cross-cc-toolchain-x64.cmake -DQT_INSTALL_PREFIX=/path/to/qt-static ..
#   make -j$(nproc)
#
# 静态链接 gcc/libstdc++ 运行时由工程 CMakeLists.txt 的 MINGW 分支负责 (-static 或 -static-libgcc -static-libstdc++)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# 编译器: 使用 PATH 中的 x86_64-w64-mingw32 系列交叉工具
set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# mingw 系统库位置: 静态 Qt 的 CMake 配置 find_library 需定位 mpr/userenv/version/ws2_32/advapi32 等
if(NOT DEFINED MINGW_SYSROOT_LIB)
    set(MINGW_SYSROOT_LIB /home/srdev/devdisk/work/cc-toolchain/mingw64/usr/x86_64-w64-mingw32/lib)
endif()

# 本机 Qt5 安装位置: 默认动态 qtwin, 静态构建可 -DQT_INSTALL_PREFIX 覆盖
if(NOT DEFINED QT_INSTALL_PREFIX OR QT_INSTALL_PREFIX STREQUAL "")
    set(QT_INSTALL_PREFIX /home/srdev/devdisk/work/cc-toolchain/qtwin)
endif()

# 查找路径:
#   - 静态 Qt 的 CMake 配置 (Qt5CoreConfig 等) 用 find_library 搜库; 静态库在 qt-static,
#     系统库 (mpr/userenv/...) 在 mingw 系统库目录; ONLY 模式下需在 CMAKE_FIND_ROOT_PATH 中列出。
list(APPEND CMAKE_FIND_ROOT_PATH ${QT_INSTALL_PREFIX} ${MINGW_SYSROOT_LIB})

# 交叉查找策略: 程序在主机上找, 库/头文件只在目标根 (qt-static + mingw 系统库) 下找
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 让 find_package(Qt5 ...) 定位 Qt (可被 -DCMAKE_PREFIX_PATH 覆盖)
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH ${QT_INSTALL_PREFIX})
endif()
