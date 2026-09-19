# cross-mingw32-static.cmake — Linux -> Windows x86 (i686) + 静态 Qt 5.15.18
# 注意: 使用 posix 线程模型编译器 (与 Qt 静态包的 winpthread 匹配)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(CMAKE_C_COMPILER   /usr/bin/i686-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER /usr/bin/i686-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER  /usr/bin/i686-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /home/srdev/qt-static/5.15.18-static/mingw810_32 /usr/i686-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_PREFIX_PATH /home/srdev/qt-static/5.15.18-static/mingw810_32)
set(USE_STATIC_QT ON CACHE BOOL "" FORCE)
