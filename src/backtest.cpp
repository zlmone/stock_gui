// backtest.cpp — Lua 策略回测引擎实现
#include "backtest.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <dirent.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

std::string resolveStrategiesDir() {
    std::string p1 = exeDir() + "/strategies";
    DIR* d = opendir(p1.c_str());
    if (d) { closedir(d); return p1; }
    std::string p2 = "/home/srdev/trade/stock_gui/strategies";
    d = opendir(p2.c_str());
    if (d) { closedir(d); return p2; }
    return p1;
}

bool luaStrategyHas(const std::string& luaPath, const char* funcName) {
    lua_State* L = luaL_newstate();
    if (!L) return false;
    luaL_openlibs(L);
    bool has = false;
    if (luaL_dofile(L, luaPath.c_str()) == LUA_OK) {
        lua_getglobal(L, funcName);
        has = lua_isfunction(L, -1) != 0;
    }
    lua_close(L);
    return has;
}

// 把参数值注入 Lua 全局 P 表 (覆盖策略文件里的默认值)
static void injectParams(lua_State* L, const std::vector<StrategyParam>* params) {
    if (!params || params->empty()) return;
    lua_getglobal(L, "P");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "P");
        lua_getglobal(L, "P");
    }
    for (const auto& p : *params) {
        lua_pushnumber(L, p.value);
        lua_setfield(L, -2, p.key.c_str());
    }
    lua_pop(L, 1);
}

std::vector<StrategyParam> luaReadParams(const std::string& luaPath) {
    std::vector<StrategyParam> out;
    lua_State* L = luaL_newstate();
    if (!L) return out;
    luaL_openlibs(L);
    if (luaL_dofile(L, luaPath.c_str()) == LUA_OK) {
        lua_getglobal(L, "params");
        if (lua_istable(L, -1)) {
            int n = (int)lua_rawlen(L, -1);
            for (int i = 1; i <= n; i++) {
                lua_geti(L, -1, i);
                if (lua_istable(L, -1)) {
                    StrategyParam sp;
                    auto gets = [&](const char* k) -> std::string {
                        lua_getfield(L, -1, k);
                        std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
                        lua_pop(L, 1);
                        return v;
                    };
                    auto getn = [&](const char* k, double def) -> double {
                        lua_getfield(L, -1, k);
                        double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
                        lua_pop(L, 1);
                        return v;
                    };
                    sp.key = gets("key");
                    sp.label = gets("label");
                    if (sp.label.empty()) sp.label = sp.key;
                    sp.value = getn("value", 0);
                    sp.min = getn("min", 0);
                    sp.max = getn("max", 100);
                    sp.step = getn("step", 0.1);
                    lua_getfield(L, -1, "int");
                    sp.isInt = lua_toboolean(L, -1) != 0;
                    lua_pop(L, 1);
                    if (!sp.key.empty()) out.push_back(sp);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    lua_close(L);
    return out;
}

std::vector<std::string> Backtest::strategyFiles() const {
    std::vector<std::string> out;
    DIR* d = opendir(dir_.c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name.size() > 4 && name.substr(name.size() - 4) == ".lua")
            out.push_back(name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

std::string Backtest::displayName(const std::string& file) const {
    std::string name = file;
    lua_State* L = luaL_newstate();
    if (!L) return name;
    luaL_openlibs(L);
    std::string path = dir_ + "/" + file;
    if (luaL_dofile(L, path.c_str()) == LUA_OK) {
        lua_getglobal(L, "strategy");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "name");
            if (lua_isstring(L, -1)) name = lua_tostring(L, -1);
        }
    }
    lua_close(L);
    return name;
}

BacktestResult Backtest::run(const std::vector<Bar>& bars, const std::string& luaFile,
                        const std::vector<StrategyParam>* params) const {
    BacktestResult res;
    size_t n = bars.size();
    if (n == 0) { res.error = "无K线数据(代码未匹配, 可试 002730 / 002730.SZ / sz002730 格式)"; return res; }
    if (n < 30) { res.error = "K线不足30根(仅" + std::to_string(n) + "根)"; return res; }

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    std::string path = dir_ + "/" + luaFile;
    if (luaL_dofile(L, path.c_str()) != LUA_OK) {
        res.error = std::string("脚本加载失败: ") + lua_tostring(L, -1);
        lua_close(L);
        return res;
    }
    // 策略名
    lua_getglobal(L, "strategy");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "name");
        if (lua_isstring(L, -1)) res.strategy_name = lua_tostring(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    // on_bar
    lua_getglobal(L, "on_bar");
    if (!lua_isfunction(L, -1)) {
        res.error = "脚本缺少 on_bar(ctx) 函数";
        lua_close(L);
        return res;
    }
    int fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    injectParams(L, params);   // 界面参数注入(覆盖默认)

    // ---------- 预计算指标 ----------
    std::vector<double> ma5(n, 0), ma10(n, 0), ma20(n, 0), ma60(n, 0), pos60(n, -1), zt20(n, 0);
    auto calcMA = [&](int m, std::vector<double>& out) {
        double sum = 0;
        for (size_t i = 0; i < n; i++) {
            sum += bars[i].close;
            if ((int)i >= m) sum -= bars[i - m].close;
            out[i] = ((int)i >= m - 1) ? sum / m : 0.0;
        }
    };
    calcMA(5, ma5); calcMA(10, ma10); calcMA(20, ma20); calcMA(60, ma60);
    for (size_t i = 59; i < n; i++) {
        double hi = bars[i].high, lo = bars[i].low;
        for (size_t j = i - 59; j <= i; j++) { hi = std::max(hi, bars[j].high); lo = std::min(lo, bars[j].low); }
        pos60[i] = (hi > lo) ? (bars[i].close - lo) / (hi - lo) * 100.0 : 0.0;
    }
    for (size_t i = 1; i < n; i++) {
        int cnt = 0;
        size_t start = (i >= 20) ? i - 19 : 1;
        for (size_t j = start; j <= i; j++) {
            double pc = (bars[j].close - bars[j-1].close) / bars[j-1].close * 100.0;
            if (pc >= 9.8) cnt++;
        }
        zt20[i] = cnt;
    }

    // ---------- 回测主循环 ----------
    lua_newtable(L);
    int ctx_idx = lua_gettop(L);
    auto setNum = [&](const char* k, double v) { lua_pushnumber(L, v); lua_setfield(L, ctx_idx, k); };
    auto setStr = [&](const char* k, const char* v) { lua_pushstring(L, v); lua_setfield(L, ctx_idx, k); };
    auto setBool = [&](const char* k, bool v) { lua_pushboolean(L, v ? 1 : 0); lua_setfield(L, ctx_idx, k); };
    auto setMA = [&](const char* k, const std::vector<double>& arr, size_t idx) {
        if (idx < n && arr[idx] > 0) lua_pushnumber(L, arr[idx]); else lua_pushnil(L);
        lua_setfield(L, ctx_idx, k);
    };

    // 整段序列数组(一次构造): on_bar 里可用 ctx.xxx_arr[ctx.i] 索引, 便于形态判断
    auto putFullArr = [&](const char* key, auto getter) {
        lua_newtable(L);
        for (size_t k = 0; k < n; k++) {
            lua_pushnumber(L, getter(k));
            lua_seti(L, -2, (lua_Integer)(k + 1));
        }
        lua_setfield(L, ctx_idx, key);
    };
    putFullArr("open_arr",  [&](size_t k) { return bars[k].open; });
    putFullArr("high_arr",  [&](size_t k) { return bars[k].high; });
    putFullArr("low_arr",   [&](size_t k) { return bars[k].low; });
    putFullArr("close_arr", [&](size_t k) { return bars[k].close; });
    putFullArr("vol_arr",   [&](size_t k) { return bars[k].volume; });
    putFullArr("ma5_arr",   [&](size_t k) { return ma5[k]; });
    putFullArr("ma10_arr",  [&](size_t k) { return ma10[k]; });
    putFullArr("ma20_arr",  [&](size_t k) { return ma20[k]; });
    setNum("n_all", (double)n);

    bool holding = false;
    double buy_price = 0;
    int buy_i = 0;
    std::string buy_date;
    double equity = 1.0, worst = 0, sum_gain = 0, sum_days = 0;
    int wins = 0;

    for (size_t i = 1; i < n; i++) {
        double pc = bars[i-1].close > 0 ? (bars[i].close - bars[i-1].close) / bars[i-1].close * 100.0 : 0.0;
        setNum("i", (double)i);
        setStr("date", bars[i].date.c_str());
        setNum("open", bars[i].open);
        setNum("high", bars[i].high);
        setNum("low", bars[i].low);
        setNum("close", bars[i].close);
        setNum("pct", pc);
        setBool("holding", holding);
        if (holding) {
            setNum("buy_price", buy_price);
            setNum("gain", (bars[i].close - buy_price) / buy_price);
            setNum("days", (double)((int)i - buy_i));
        } else {
            setNum("buy_price", 0); setNum("gain", 0); setNum("days", 0);
        }
        setMA("ma5", ma5, i); setMA("ma10", ma10, i); setMA("ma20", ma20, i); setMA("ma60", ma60, i);
        setMA("ma5_prev", ma5, i - 1); setMA("ma20_prev", ma20, i - 1);
        if (pos60[i] >= 0) setNum("pos60", pos60[i]); else { lua_pushnil(L); lua_setfield(L, ctx_idx, "pos60"); }
        setNum("zt20", zt20[i]);

        lua_rawgeti(L, LUA_REGISTRYINDEX, fn_ref);
        lua_pushvalue(L, ctx_idx);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            res.error = std::string("on_bar 执行错误: ") + lua_tostring(L, -1);
            lua_close(L);
            return res;
        }
        std::string signal;
        if (lua_isstring(L, -1)) signal = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (!holding && signal == "BUY") {
            holding = true; buy_price = bars[i].close; buy_i = (int)i; buy_date = bars[i].date;
        } else if (holding && signal == "SELL") {
            Trade t;
            t.buy_index = buy_i; t.sell_index = (int)i;
            t.buy_date = buy_date; t.sell_date = bars[i].date;
            t.buy_price = buy_price; t.sell_price = bars[i].close;
            t.gain_pct = (t.sell_price - t.buy_price) / t.buy_price * 100.0;
            t.days = (int)i - buy_i;
            t.reason = "信号";
            res.trades.push_back(t);
            equity *= (1.0 + t.gain_pct / 100.0);
            worst = std::min(worst, t.gain_pct);
            sum_gain += t.gain_pct; sum_days += t.days;
            if (t.gain_pct > 0) wins++;
            holding = false;
        }
    }
    if (holding) {
        Trade t;
        t.buy_index = buy_i; t.sell_index = (int)n - 1;
        t.buy_date = buy_date; t.sell_date = bars[n-1].date;
        t.buy_price = buy_price; t.sell_price = bars[n-1].close;
        t.gain_pct = (t.sell_price - t.buy_price) / t.buy_price * 100.0;
        t.days = (int)n - 1 - buy_i;
        t.reason = "到期平仓";
        res.trades.push_back(t);
        equity *= (1.0 + t.gain_pct / 100.0);
        worst = std::min(worst, t.gain_pct);
        sum_gain += t.gain_pct; sum_days += t.days;
        if (t.gain_pct > 0) wins++;
    }

    res.ok = true;
    res.n_trades = (int)res.trades.size();
    res.total_return = (equity - 1.0) * 100.0;
    res.win_rate = res.n_trades ? (double)wins / res.n_trades * 100.0 : 0;
    res.worst_trade = worst;
    res.avg_gain = res.n_trades ? sum_gain / res.n_trades : 0;
    res.avg_days = res.n_trades ? sum_days / res.n_trades : 0;

    double eq2 = 1.0, pk2 = 1.0, mdd = 0;
    for (auto& t : res.trades) {
        eq2 *= (1.0 + t.gain_pct / 100.0);
        pk2 = std::max(pk2, eq2);
        mdd = std::min(mdd, (eq2 - pk2) / pk2 * 100.0);
    }
    res.max_drawdown = mdd;

    lua_close(L);
    return res;
}

// ==================== 形态筛选 (on_scan) ====================
ScanSession::ScanSession(const std::string& luaFile, const std::vector<StrategyParam>* params) {
    L_ = luaL_newstate();
    if (!L_) { error_ = "Lua 初始化失败"; return; }
    luaL_openlibs(L_);
    if (luaL_dofile(L_, luaFile.c_str()) != LUA_OK) {
        error_ = std::string("脚本加载失败: ") + lua_tostring(L_, -1);
        lua_close(L_); L_ = nullptr;
        return;
    }
    injectParams(L_, params);   // 界面参数注入(覆盖默认)
    // 策略名
    lua_getglobal(L_, "strategy");
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, "name");
        if (lua_isstring(L_, -1)) strategyName = lua_tostring(L_, -1);
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);

    lua_getglobal(L_, "on_scan");
    if (!lua_isfunction(L_, -1)) {
        error_ = "脚本缺少 on_scan(s) 函数 (筛选模式需要)";
        lua_close(L_); L_ = nullptr;
        return;
    }
    fn_ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    ok_ = true;
}

ScanSession::~ScanSession() {
    if (L_) lua_close(L_);
}

ScanResult ScanSession::test(const std::vector<Bar>& bars) {
    ScanResult res;
    if (!ok_ || !L_) { res.error = "会话未初始化"; return res; }
    size_t n = bars.size();
    if (n < 30) { res.ok = true; res.passed = false; return res; }

    // 窗口: 最近 60 根 (潜伏池等策略需 60 日回看)
    const int W = 60;
    size_t start = (n > (size_t)W) ? n - W : 0;
    int m = (int)(n - start);              // 窗口长度

    // 均线(用完整序列计算, 取窗口切片)
    auto calcMAarr = [&](int period, std::vector<double>& out) {
        out.assign(m, 0.0);
        for (int k = 0; k < m; k++) {
            size_t idx = start + k;
            if ((int)idx >= period - 1) {
                double sum = 0;
                for (int j = 0; j < period; j++) sum += bars[idx - j].close;
                out[k] = sum / period;
            }
        }
    };
    std::vector<double> ma5, ma10, ma20;
    calcMAarr(5, ma5); calcMAarr(10, ma10); calcMAarr(20, ma20);

    // 构造 s table
    lua_newtable(L_);
    int s_idx = lua_gettop(L_);
    auto putArr = [&](const char* key, auto getter) {
        lua_newtable(L_);
        for (int k = 0; k < m; k++) {
            lua_pushnumber(L_, getter(start + k));
            lua_seti(L_, -2, k + 1);
        }
        lua_setfield(L_, s_idx, key);
    };
    putArr("open",  [&](size_t i) { return bars[i].open; });
    putArr("high",  [&](size_t i) { return bars[i].high; });
    putArr("low",   [&](size_t i) { return bars[i].low; });
    putArr("close", [&](size_t i) { return bars[i].close; });
    putArr("vol",   [&](size_t i) { return bars[i].volume; });
    putArr("pct",   [&](size_t i) {
        return i > 0 && bars[i-1].close > 0 ? (bars[i].close - bars[i-1].close) / bars[i-1].close * 100.0 : 0.0;
    });
    auto putMA = [&](const char* key, const std::vector<double>& arr) {
        lua_newtable(L_);
        for (int k = 0; k < m; k++) {
            if (arr[k] > 0) lua_pushnumber(L_, arr[k]); else lua_pushnil(L_);
            lua_seti(L_, -2, k + 1);
        }
        lua_setfield(L_, s_idx, key);
    };
    putMA("ma5", ma5); putMA("ma10", ma10); putMA("ma20", ma20);
    lua_pushinteger(L_, m); lua_setfield(L_, s_idx, "n");
    lua_pushnumber(L_, bars[n-1].close); lua_setfield(L_, s_idx, "spot");
    lua_pushstring(L_, bars[n-1].date.c_str()); lua_setfield(L_, s_idx, "date");

    // 调用 on_scan(s)
    lua_rawgeti(L_, LUA_REGISTRYINDEX, fn_ref_);
    lua_pushvalue(L_, s_idx);
    if (lua_pcall(L_, 1, 2, 0) != LUA_OK) {
        res.error = std::string("on_scan 错误: ") + lua_tostring(L_, -1);
        lua_settop(L_, s_idx - 1);
        return res;
    }
    res.ok = true;
    res.passed = lua_toboolean(L_, -2) != 0;
    if (lua_isstring(L_, -1)) res.detail = lua_tostring(L_, -1);
    lua_settop(L_, s_idx - 1);   // 清理栈(去掉 s + 两个返回值)
    return res;
}
