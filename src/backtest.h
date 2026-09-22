// backtest.h — Lua 策略回测引擎
// 策略文件: strategies/*.lua, 每个文件定义 strategy 表 + on_bar(ctx) 函数
// on_bar 返回 "BUY" / "SELL" / "HOLD", 引擎以收盘价成交并统计
#pragma once
#include <string>
#include <vector>
#include "engine.h"

struct lua_State;

struct Trade {
    int buy_index = 0, sell_index = 0;
    std::string buy_date, sell_date;
    double buy_price = 0, sell_price = 0;
    double gain_pct = 0;
    int days = 0;
    std::string reason;     // 卖出原因: 信号 / 到期平仓
};

struct BacktestResult {
    bool ok = false;
    std::string error;
    std::string code, name;          // 由调用方填充
    std::string strategy_name;
    std::vector<Trade> trades;
    int n_trades = 0;
    double total_return = 0;   // 复利累计 %
    double win_rate = 0;       // %
    double worst_trade = 0;    // 最差单笔 %
    double avg_gain = 0;
    double avg_days = 0;
    double max_drawdown = 0;   // 交易级最大回撤 %
};

// ==================== 策略参数 (界面化) ====================
// 策略文件可声明全局表 params = { {key,label,min,max,step,value,[int]}, ... }
// 引擎读取后渲染到界面; 运行时把界面值注入 Lua 全局 P 表 (覆盖默认)
struct StrategyParam {
    std::string key, label;
    double value = 0, min = 0, max = 100, step = 1;
    bool isInt = false;
};

// 读取策略文件的 params 定义 (无则返回空)
std::vector<StrategyParam> luaReadParams(const std::string& luaPath);

// Lua 语法预检: 只编译不运行; 返回空串=通过, 否则为 Lua 错误信息 (供 GUI 保存/运行前拦截)
std::string luaSyntaxCheck(const std::string& luaPath);

// 把界面参数写回 .lua 文件的 params 块: 仅替换各 key 的 value= 令牌, 保留注释/格式/逻辑
// 返回实际写入的参数个数; 若 params 块缺失或文件不可写返回 0 (调用方可提示)
int luaWriteParams(const std::string& luaPath, const std::vector<StrategyParam>& params);

// 写整份 .lua 源码 (编辑器工作室 P1-B): 先编译校验新内容(luaSyntaxCheck),
// 无语法错误且非空才落盘 (写前 .bak 备份旧文件); errMsg 填失败原因。
// 返回是否写入成功 (false=未改动磁盘, 调用方可提示)。
bool luaWriteStrategyFile(const std::string& luaPath, const std::string& content, std::string* errMsg);

class Backtest {
public:
    explicit Backtest(const std::string& strategiesDir) : dir_(strategiesDir) {}
    ~Backtest() {}

    std::string dir() const { return dir_; }
    std::vector<std::string> strategyFiles() const;              // 目录下所有 .lua
    std::string displayName(const std::string& file) const;      // 读取 strategy.name

    // 对一只股票的K线序列运行策略回测 (params 非空时注入 Lua 全局 P 表)
    BacktestResult run(const std::vector<Bar>& bars, const std::string& luaFile,
                       const std::vector<StrategyParam>* params = nullptr) const;

private:
    std::string dir_;
};

// 策略目录解析: exe同目录/strategies > 开发目录
std::string resolveStrategiesDir();

// 探测策略文件是否定义了指定函数 (如 "on_bar" / "on_scan")
bool luaStrategyHas(const std::string& luaPath, const char* funcName);


// ==================== 形态筛选 (on_scan) ====================
// 策略文件可定义 on_scan(s) 函数做"选股": 返回 true(命中) / false, 可附带备注字符串
// s 提供近期序列: s.n(天数,1=最早 n=最新), s.open/high/low/close/vol/pct[i],
//   s.ma5/ma10/ma20[i], s.code, s.spot(现价)
// 用法: 构造一次 ScanSession(加载脚本), 对全市场逐只 test(bars)
struct ScanResult {
    bool ok = false;         // 脚本执行无错误
    bool passed = false;     // 命中
    std::string error;
    std::string detail;      // on_scan 第二个返回值(备注)
};

class ScanSession {
public:
    explicit ScanSession(const std::string& luaFile,
                         const std::vector<StrategyParam>* params = nullptr);   // 完整路径
    ~ScanSession();
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    std::string strategyName;                            // strategy.name

    // 用最近30根K线做一次筛选; rps>0 时注入 s.rps(全市场相对强度百分位),
    // 供策略做横截面过滤 —— 单只股票自己算不出这个值, 必须由引擎从全市场算好传入。
    // ff: 可选的"当日资金流"(由 netclient 抓取落库 fundflow.db, 调用方读好后传进来);
    //     非空且 date 非空时注入 s.main_net/s.main_pct/s.super_net/s.big_net/s.mid_net/
    //     s.small_net(单位: 万元) 与 s.ff_date, 供脚本做"主力净流入>X 且涨幅温和"这类选股。
    ScanResult test(const std::vector<Bar>& bars, double rps = 0.0,
                    const FundFlowDay* ff = nullptr);

private:
    lua_State* L_ = nullptr;
    int fn_ref_ = -1;
    bool ok_ = false;
    std::string error_;
};
