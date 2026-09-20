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
| `stock_gui.exe` | GUI 主程序 |
| `engine_test.exe` | 引擎命令行工具 |
| `bt_test.exe` | 策略回测工具 |
| `Qt5Core/Gui/Network/Widgets.dll` | Qt5 运行期库（**已附带**） |
| `platforms/qwindows.dll` | Qt5 Windows 平台插件 |
| `start.bat` | 启动脚本 |

**已附带全部运行期依赖**，解压即可运行，目标机器无需安装 Qt。
（属于"动态链接 Qt5"构建；若需单文件零依赖的静态版，见下。）

## 四、本地自行构建

```bash
./build.sh                                  # 交叉编译 -> build_release_x64/*.exe
./scripts/deploy_win.sh build_release_x64   # 打包 -> stock_gui-win64.zip
```

## 五、关于静态版本

当前发布包是**动态链接 Qt5**（约 12MB，需随带上面那些 DLL，已打包在内）。
README 里提到的"纯静态零依赖"版需要宿主机上有静态 Qt 安装
（`/home/srdev/qt-static/...`），目前 runner 上未安装，所以走的是动态版。
