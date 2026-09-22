cmake_minimum_required(VERSION 3.10)
project(stock_gui C CXX)

option(USE_STATIC_QT "Link Qt statically" OFF)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ---- MinGW 交叉编译: 静态链接 gcc/stdc++ 运行时 ----
if(MINGW)
    if(USE_STATIC_QT)
        add_link_options(-static)
    else()
        add_link_options(-static-libgcc -static-libstdc++)
    endif()
endif()

# ---- 通用: sqlite3 源码 (复用工作区已有版本) ----
set(SQLITE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3/sqlite3.c)
set_source_files_properties(${SQLITE_SRC} PROPERTIES COMPILE_OPTIONS "-w")

# ---- Lua 5.4 静态库 (策略引擎) ----
file(GLOB LUA_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua/*.c)
add_library(lua54 STATIC ${LUA_SRCS})
set_source_files_properties(${LUA_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(lua54 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua)

# ---- 1) 引擎命令行测试 (纯C++, 任何平台) ----
add_executable(engine_test
    src/main_test.cpp
    src/engine.cpp
    ${SQLITE_SRC}
)
target_include_directories(engine_test PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3)
if(WIN32)
    target_link_libraries(engine_test PRIVATE )
else()
    target_link_libraries(engine_test PRIVATE pthread dl)
endif()

# ---- 1b) 策略回测命令行工具 ----
add_executable(bt_test
    src/bt_test.cpp
    src/engine.cpp
    src/backtest.cpp
    ${SQLITE_SRC}
)
target_include_directories(bt_test PRIVATE src third_party/lua ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3)
target_link_libraries(bt_test PRIVATE lua54)
if(NOT WIN32)
    target_link_libraries(bt_test PRIVATE pthread dl)
endif()

# ---- 2) 新闻采集核心单测 (纯 C++, 不联网, 任何平台) ----
add_executable(news_test
    src/twp_news.cpp
    src/news.cpp
    ${SQLITE_SRC}
)
target_include_directories(news_test PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3)
if(NOT WIN32)
    target_link_libraries(news_test PRIVATE pthread dl)
endif()

# ---- 3) Qt GUI 主程序 (检测到 Qt5 才构建) ----
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt5 COMPONENTS Widgets Network QUIET)
if(Qt5_FOUND)
    add_executable(stock_gui WIN32
        src/main.cpp
        src/mainwindow.cpp
        src/netclient.cpp
        src/engine.cpp
        src/backtest.cpp
        src/klinechart.cpp
        src/news.cpp
        src/pool.cpp
        ${SQLITE_SRC}
    )
    target_include_directories(stock_gui PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3)

    # ---- QScintilla: 专用 Lua 代码编辑器 (内部静态构建, 见 scripts/build_qscintilla_static.sh) ----
    #   提供行号栏 / 括号匹配 / 自动缩进 / 官方 Lua 词法器 / 折叠等, 取代原先的 QTextEdit + 自写高亮.
    set(QSCINTILLA_PREFIX "/home/srdev/devdisk/qscintilla-install" CACHE PATH "QScintilla 静态安装前缀")
    if(NOT EXISTS "${QSCINTILLA_PREFIX}/include/Qsci/qsciscintilla.h")
        message(FATAL_ERROR "未找到 QScintilla: ${QSCINTILLA_PREFIX} (请先运行 scripts/build_qscintilla_static.sh)")
    endif()
    message(STATUS "QScintilla: ${QSCINTILLA_PREFIX}")
    target_include_directories(stock_gui PRIVATE "${QSCINTILLA_PREFIX}/include")
    target_link_libraries(stock_gui PRIVATE "${QSCINTILLA_PREFIX}/lib/libqscintilla2_qt5.a")

    target_link_libraries(stock_gui PRIVATE lua54)
    # Qt 库: 静态时链接 libQt5*.a (USE_STATIC_QT=ON), 动态时链接 Qt5*.dll 导入库
    target_link_libraries(stock_gui PRIVATE Qt5::Widgets Qt5::Network)
    if(USE_STATIC_QT)
        message(STATUS "Static Qt: 链接 libQt5*.a 静态库 (QT_INSTALL_PREFIX=" ${QT_INSTALL_PREFIX} ")")
    endif()
    # 平台插件:
    #   静态构建 (USE_STATIC_QT=ON): 链接 Qt5::QWindowsIntegrationPlugin 目标; 静态 Qt 下该目标含
    #     静态 QPA 注册入口 qt_static_plugin_QWindowsIntegrationPlugin, 自动注册为 QPA, 无需随包附 DLL.
    #   动态构建: 链接 DLL 导入库, 发布时随发布包附 platforms/qwindows.dll.
    if(TARGET Qt5::QWindowsIntegrationPlugin)
        target_link_libraries(stock_gui PRIVATE Qt5::QWindowsIntegrationPlugin)
        message(STATUS "QWindowsIntegrationPlugin linked")
    endif()
    # 诊断/无头模式平台插件 (QT_QPA_PLATFORM=minimal|offscreen)
    # 仅动态构建链接 (静态构建下这些静态插件需单独的 Q_IMPORT_PLUGIN, 暂不启用)
    if(NOT USE_STATIC_QT)
        if(TARGET Qt5::QMinimalIntegrationPlugin)
            target_link_libraries(stock_gui PRIVATE Qt5::QMinimalIntegrationPlugin)
        endif()
        if(TARGET Qt5::QOffscreenIntegrationPlugin)
            target_link_libraries(stock_gui PRIVATE Qt5::QOffscreenIntegrationPlugin)
        endif()
    endif()
    if(NOT WIN32)
        target_link_libraries(stock_gui PRIVATE pthread dl)
    endif()
    message(STATUS "Qt5 found -> stock_gui will be built")
else()
    message(STATUS "Qt5 NOT found -> GUI target skipped (engine_test still available)")
endif()
