# stock_gui — 股票四维体检工具 (C++ / Qt5)

本地数据库 + 网络数据的一体化股票分析桌面工具。
四维 = 行为(修复率/回测) + 位置(均线/区间) + 估值(PB分位) + 归类(行业/概念)

## 架构

```
src/
├── engine.h/.cpp     核心引擎: SQLite读取 + 四维指标 (纯C++, 无GUI依赖)
├── mainwindow.h/.cpp Qt主窗口: 输入代码 -> 体检 -> 表格展示
├── netclient.h/.cpp  网络层: 腾讯快照/新浪K线 (Qt Network, GBK转码)
├── main.cpp          入口
└── (链接 ../stock_analyzer/sqlite3.c 自带SQLite)
```

数据源:
- `../kline_latest.db`         最新窗口(60天) + 股票名称 + 行业/概念CSV配套
- `../kline_full_history.db`   全历史K线 + PE/PB估值(3年分位)
- 网络(可选): qt.gtimg.cn 快照 / quotes.sina.cn K线

## Linux 构建 (当前环境已验证)

```bash
cd stock_gui && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
./engine_test 002730.SZ 603106.SH     # 命令行引擎测试
./stock_gui                            # GUI (需要图形环境)
```

## Windows / VC++ 构建 (目标平台)

1. 安装 Qt 5.15: https://download.qt.io/archive/qt/ (选 MSVC 2019 64-bit 组件)
2. 安装 Visual Studio 2019/2022 (含 C++ 桌面开发负载) + Qt VS Tools 插件
3. 打开 `CMakeLists.txt` (VS 2019+ 原生支持 CMake 工程):
   - 配置 CMake 时指定 Qt5 的 CMAKE_PREFIX_PATH, 例:
     `C:\Qt\5.15.2\msvc2019_64`
4. 生成 → 编译 → 得到 `stock_gui.exe` (纯 VC++ 程序)
   - 注意: 数据库路径 `LATEST_DB/HIST_DB` 在两个源文件顶部常量, 部署时按需修改
   - WIN32 目标已配置 (无控制台窗口)

## 当前功能 (v0.1)

- [x] 引擎: 拼接双库K线(全历史+最新窗口), 计算 修复率/回测/最差单笔/涨停/均线/位置
- [x] 引擎: PB估值分位 (kline_full_history.db/valuation_daily 近3年)
- [x] GUI: 输入代码 -> 表格体检
- [x] 默认池持久化: 存在 exe 同目录的 pool.db (设 STOCK_DB_DIR 则放该目录);
      结果表(体检/形态筛选/回测/资金流/盯盘)右键 ->【加入默认池】, 或个股详情里点【☆ 加入默认池】;
      左侧【池管理…】可删/清空/导出CSV/粘贴导入。首次运行会把老的 19 只种子写进库, 之后全由库驱动
- [x] 资金流页: 东财口径当日全市场主力净流入/净占比/超大单/大单/中单/小单, 落库 fundflow.db; 可导出CSV;
      Lua 策略里可用 s.main_net / s.main_pct (万元, 见 strategies/66_money_flow.lua)
- [x] 网络层骨架: 腾讯快照/新浪K线 (fetchSnapshots/fetchLatestKline)
- [ ] TODO v0.2: 网络层实跑注入PB + 双击行看K线详情
- [ ] TODO v0.3: 扫描器(全市场跑分排序) + 数据刷新(拉最新K线入库)
- [ ] TODO v0.4: 触发价提醒

## 代码风格

C++14, 中文注释, 与工作区既有 C++ 代码( stock_analyzer.cpp 等)保持一致风格。


## 交叉编译（Linux/WSL → Windows，无需 Windows 环境）

本工作区已配好两套交叉工具链：

### A. 动态版（i686 + 共享 Qt 5.15.7）
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=../cross-mingw32.cmake ..
make -j4    # 产物需附带 Qt5*.dll + platforms/qwindows.dll 分发
```

### B. 静态版（x86-64 静态 Qt 5.15.0）★推荐
```bash
USE_STATIC_QT=ON ./build.sh
# 产物: build_static_x64/  (stock_gui.exe / engine_test.exe / bt_test.exe)
```
要点：产物为零依赖单文件 exe（约 37MB），把整个 Qt5（Core/Gui/Network/Widgets）、
Qt 平台插件(qwindows) 及运行期库全部静态编进 exe，拷贝即用。
静态 Qt 交叉编译并安装于 `/home/srdev/devdisk/qt-static`，
构建工具链为 `cross-cc-toolchain-x64.cmake`（find_package(Qt5) 定位到该目录）。
```
> 注：旧的 32 位静态工具链 `cross-mingw32-static.cmake`（指向
> /home/srdev/qt-static/5.15.18-static/mingw810_32）当前静态 Qt 尚未重建，暂不可用。

### wine 冒烟测试
```bash
STOCK_DB_DIR="Z:/home/srdev/trade" wine engine_test.exe 002730.SZ
```


## 交付包 (v0.1)

`package/stock_gui_v0.1_win32.zip` (84 MB) —— 解压即跑, 零依赖:

```
stock_gui_win32/
├── stock_gui.exe      静态编译GUI (35.4MB, 无Qt/无运行库依赖)
├── engine_test.exe    命令行版 (2.3MB)
├── kline_latest.db    行情库: 5113只 × 260根, 截至 2026-09-18
├── valuation.db       估值库: 近5年月采样 PE/PB (截至 2026-05-14)
└── 使用说明.txt
```

特性:
- 数据库自动发现: exe 同目录 / STOCK_DB_DIR 环境变量 / 开发机路径
- 估值库缺失时自动降级 (估值分位显示 "-", 不影响核心功能)
- 数据更新: `fetch_latest_kline.py` + `fill_stock_names.py` + `export_valuation.py`
