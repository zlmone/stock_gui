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

# ---- 2) Qt GUI 主程序 (检测到 Qt5 才构建) ----
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
        ${SQLITE_SRC}
    )
    target_include_directories(stock_gui PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3)
    target_link_libraries(stock_gui PRIVATE Qt5::Widgets Qt5::Network)
    target_link_libraries(stock_gui PRIVATE lua54)
    # 静态 Qt: 链接 Windows 平台插件(内含 Q_IMPORT_PLUGIN 自动导入)
    if(TARGET Qt5::QWindowsIntegrationPlugin)
        target_link_libraries(stock_gui PRIVATE Qt5::QWindowsIntegrationPlugin)
        message(STATUS "Static Qt: QWindowsIntegrationPlugin linked")
    endif()
    # 诊断/无头模式平台插件 (QT_QPA_PLATFORM=minimal|offscreen)
    if(TARGET Qt5::QMinimalIntegrationPlugin)
        target_link_libraries(stock_gui PRIVATE Qt5::QMinimalIntegrationPlugin)
    endif()
    if(TARGET Qt5::QOffscreenIntegrationPlugin)
        target_link_libraries(stock_gui PRIVATE Qt5::QOffscreenIntegrationPlugin)
    endif()
    if(NOT WIN32)
        target_link_libraries(stock_gui PRIVATE pthread dl)
    endif()
    message(STATUS "Qt5 found -> stock_gui will be built")
else()
    message(STATUS "Qt5 NOT found -> GUI target skipped (engine_test still available)")
endif()
