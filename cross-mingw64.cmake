# cross-mingw64.cmake — Linux -> Windows x64 (MinGW-w64) 交叉编译工具链
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# 只在这两个根下找库/头文件（优先 Windows 版 Qt）
set(CMAKE_FIND_ROOT_PATH /home/srdev/qt5-build-mingw32/install /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt 位置
set(CMAKE_PREFIX_PATH /home/srdev/qt5-build-mingw32/install)
