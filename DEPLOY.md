# stock_gui 部署说明（Windows x86-64）

本项目由 Gitea Actions runner **自动编译并发布**：每次 push 到 `master`，
runner 会交叉编译出 Windows 可执行文件、打包成 zip，上传到本仓库的 **Release**。

- 发布页：http://10.168.1.242:3000/srdev/stock_gui/releases
- 仓库为**私有**，下载需登录：账号 `srdev`，密码 `king2014`

---

## 一、在别的 Windows 电脑上使用（推荐：浏览器）

1. 打开 http://10.168.1.242:3000/srdev/stock_gui/releases
2. 用 `srdev` / `king2014` 登录
3. 下载**最新**那个 `stock_gui_<短sha>_<日期时间>.zip`
4. 解压到一个目录（例如 `D:\stock_gui`）
5. 双击 **`start.bat`**（或直接双击 `stock_gui.exe`）运行

包名带提交号和打包时间，方便确认下载到的是不是最新构建。

## 二、命令行下载

### PowerShell（Windows 自带）

```powershell
# 一键下载最新发布包到当前目录
powershell -ExecutionPolicy Bypass -File scripts\download_latest.ps1
```

### curl（Windows 10+ / Linux / macOS）

```bash
# 直接下某个 tag 的包
curl -u srdev:king2014 -O \
  http://10.168.1.242:3000/srdev/stock_gui/releases/download/5dc5885/stock_gui_5dc5885_20260920-0915.zip
```

## 三、包里有什么

| 文件 | 说明 |
|---|---|
| `stock_gui.exe` | GUI 主程序（静态 Qt，约 37MB，零 DLL 依赖） |
| `engine_test.exe` | 引擎命令行工具 |
| `bt_test.exe` | 策略回测工具 |
| `strategies/` | 6 个内建策略脚本（.lua） |
| `concept_map.csv` | 概念映射表 |
| `industry_map.csv` | 行业映射表 |
| `kline_latest.db` | 行情主库（约 316MB，含全市场 K 线） |
| `valuation.db` | 估值库（PB/PE 分位，约 26MB） |
| `start.bat` | 启动脚本 |
| `使用说明.txt` | 使用说明 |

- 数据库/映射表随包附带，解包即可使用（exe 同目录查找，或设 `STOCK_DB_DIR` 指定其它位置）。
- 已把整个 Qt5（Core/Gui/Widgets/Network）、Qt 平台插件及运行期库全部静态编译进 exe，
  目标机器无需安装 Qt 或任何运行库。
- 发布包较大主因是随包带了约 340MB 行情库；运行期自身零依赖。

## 四、本地自行构建

```bash
./build.sh                                  # 动态 Qt -> build_release_x64/*.exe
./scripts/deploy_win.sh build_release_x64   # 打包（附 Qt DLL）

USE_STATIC_QT=ON ./build.sh                 # 静态 Qt -> build_static_x64/*.exe（零依赖单文件）
./scripts/deploy_win.sh build_static_x64    # 打包（附策略/数据库/映射表）
```
静态构建需宿主机已交叉编译安装静态 Qt（`/home/srdev/devdisk/qt-static`）；
部署数据源目录可用 `DEPLOY_DATA_DIR=…` 覆盖。

## 五、关于静态版本

当前发布包即为**静态 Qt 零依赖**版。构建前提是宿主机已交叉编译静态 Qt，
静态 Qt 安装于 `/home/srdev/devdisk/qt-static`；CI runner 与本机同一宿主机，故可访问。
