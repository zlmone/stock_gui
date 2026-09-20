-- ============================================================
-- bt_harness.lua -- 复刻 backtest.cpp 主循环, 调用真实策略 on_bar
-- 用法: ./runlua bt_harness.lua <数据.csv> <策略.lua> [最近N根]
-- 数据列: date,open,high,low,close,volume  (按日期升序)
-- 目的: 在 Linux (无 C++ 编译器) 用内置 Lua 验证策略 on_bar 逻辑
-- ============================================================
local csvPath, strat, maxBars = arg[1], arg[2], (arg[3] and tonumber(arg[3]) or 1e9)
if not csvPath or not strat then
    print("用法: runlua bt_harness.lua <数据.csv> <策略.lua> [最近N根]"); return
end
local function split(s)
    local out = {}
    for x in s:gmatch("([^,]*)") do out[#out+1] = x end
    return out
end

-- 1) 读 CSV -> 1 索引 cd (收盘)/ vol (量)/ od (日期)
local cd, vol, od, hi, lo, op = {}, {}, {}, {}, {}, {}
for line in io.open(csvPath, "r"):lines() do
    local f = split(line)
    if #f >= 6 then
        cd[#cd+1]  = tonumber(f[5])
        vol[#vol+1]= tonumber(f[6])
        od[#od+1]  = f[1]
        hi[#hi+1]  = tonumber(f[3])
        lo[#lo+1]  = tonumber(f[4])
        op[#op+1]  = tonumber(f[2])
    end
end
if #cd < 2 then print("数据不足:", #cd); return end

-- 2) 只取最后 maxBars 根 (与引擎 loadSeries 取末尾一致)
local n0 = #cd
if n0 > maxBars then
    local s = n0 - maxBars
    local nc, nv, nd = {}, {}, {}
    for i=s+1, n0 do nc[#nc+1]=cd[i]; nv[#nv+1]=vol[i]; nd[#nd+1]=od[i] end
    cd, vol, od = nc, nv, nd
end
local n = #cd

-- 3) 均线 calcMA (C++: out[i]=(i>=m-1) and sum/m or 0; 1 索引: out[I]=(I>=m) and sum/m or 0)
local function calcMA(m)
    local out, sum = {}, 0
    for I=1, n do
        sum = sum + cd[I]
        if I > m then sum = sum - cd[I-m] end
        out[I] = (I >= m) and (sum/m) or 0.0
    end
    return out
end
local ma5  = calcMA(5)
local ma10 = calcMA(10)
local ma20 = calcMA(20)
local ma60 = calcMA(60)
-- pos60: 近 60 日位置 (收盘在 60 日高低区间中的 %); <60 根时为 nil (与引擎一致)
local pos60 = {}
for I = 60, n do
    local hi = cd[I]
    local lo = cd[I]
    for k = I - 59, I do local v = cd[k]; if v > hi then hi = v end; if v < lo then lo = v end end
    pos60[I] = (hi > lo) and (cd[I] - lo) / (hi - lo) * 100 or 0.0
end
-- zt20: 近 20 根涨停天数 (单日涨幅 >= 9.8%)
local zt20 = {}
for I = 2, n do
    local cnt = 0; local start = I - 19; if start < 2 then start = 2 end
    for k = start, I do local pc = (cd[k] - cd[k-1]) / cd[k-1] * 100; if pc >= 9.8 then cnt = cnt + 1 end end
    zt20[I] = cnt
end

-- 4) 载入策略 (定义 strategy/params/P/on_bar)
dofile(strat)
local P = {}
if params then for _, v in ipairs(params) do P[v.key] = v.value end end

-- 5) 主循环: C++ 0 索引 i=1..n-1  <=> 1 索引 I=2..n
local holding, buy_price, buy_i, buy_date = false, 0, 0, ""
local equity, worst, sum_gain, sum_days, wins = 1.0, 0, 0, 0, 0
local trades, ctx = {}, {}

local function call()
    local ok, res = pcall(on_bar, ctx)
    if not ok then error("on_bar 异常: "..tostring(res)) end
    return res
end

for I=2, n do
    ctx.i, ctx.date, ctx.close = I-1, od[I], cd[I]
    ctx.pct = (cd[I]-cd[I-1])/cd[I-1]*100
    ctx.holding = holding
    if holding then ctx.buy_price, ctx.gain, ctx.days = buy_price, (cd[I]-buy_price)/buy_price, I-buy_i
    else ctx.buy_price, ctx.gain, ctx.days = 0, 0, 0 end
    ctx.ma5  = ma5[I]  > 0 and ma5[I]  or nil
    ctx.ma10 = ma10[I] > 0 and ma10[I] or nil
    ctx.ma20 = ma20[I] > 0 and ma20[I] or nil
    ctx.ma5_prev  = (ma5[I-1]  > 0) and ma5[I-1]  or nil
    ctx.ma20_prev = (ma20[I-1] > 0) and ma20[I-1] or nil
    ctx.close_arr, ctx.vol_arr, ctx.ma5_arr  = cd, vol, ma5
    ctx.high_arr, ctx.low_arr, ctx.open_arr  = hi, lo, op
    ctx.ma10_arr, ctx.ma20_arr, ctx.ma60_arr = ma10, ma20, ma60
    ctx.pos60 = (I >= 60) and pos60[I] or nil
    ctx.zt20 = zt20[I]
    ctx.equity = equity

    local sig = call()
    if not holding and sig=="BUY" then
        holding, buy_price, buy_i, buy_date = true, cd[I], I, od[I]
    elseif holding and sig=="SELL" then
        local t = { buy_date=buy_date, buy_price=buy_price, sell_date=od[I], sell_price=cd[I],
                    days=I-buy_i, gain_pct=(cd[I]-buy_price)/buy_price*100, reason="信号" }
        trades[#trades+1]=t; equity=equity*(1+t.gain_pct/100)
        worst = (worst==0 or t.gain_pct<worst) and t.gain_pct or worst
        sum_gain, sum_days = sum_gain+t.gain_pct, sum_days+t.days
        if t.gain_pct>0 then wins=wins+1 end
        holding=false
    end
end

if holding then
    local t = { buy_date=buy_date, buy_price=buy_price, sell_date=od[n], sell_price=cd[n],
                days=n-buy_i, gain_pct=(cd[n]-buy_price)/buy_price*100, reason="到期平仓" }
    trades[#trades+1]=t; equity=equity*(1+t.gain_pct/100)
    worst = (worst==0 or t.gain_pct<worst) and t.gain_pct or worst
    sum_gain, sum_days = sum_gain+t.gain_pct, sum_days+t.days
    if t.gain_pct>0 then wins=wins+1 end
end

-- 6) 输出
local nm = strat:gsub("^.*/", ""):gsub("\\\\", ":")
print(string.format("== 策略 %-28s | 数据 %d 根 ==", strategy and strategy.name or nm, n))
print(string.format("  成交 %d 笔 | 胜率 %.0f%% | 总收益 %+.1f%% | 最差 %+.1f%% | 平均持有 %.1f日 | 均收益 %+.1f%%",
    #trades, (#trades>0 and wins/#trades*100 or 0), (equity-1)*100, worst,
    (#trades>0 and sum_days/#trades or 0), (#trades>0 and sum_gain/#trades or 0)))
local show = (#trades<3) and #trades or 3
for k=#trades-show+1, #trades do
    local t=trades[k]
    print(string.format("    %s 买%.2f -> %s 卖%.2f | %+.1f%% %d日 (%s)",
        t.buy_date, t.buy_price, t.sell_date, t.sell_price, t.gain_pct, t.days, t.reason))
end
print("  --- 近 40 根信号轨迹 (日期  收盘   MA5  MA10 MA20 | 信号) ---")
for I=n-39, n do
    if I<2 then goto cont end
    local sig="-"
    if not holding then
        ctx.i=I-1; ctx.close=cd[I]; ctx.holding=false
        ctx.buy_price=0; ctx.gain=0; ctx.days=0
        ctx.ma5=ma5[I]>0 and ma5[I] or nil; ctx.ma10=ma10[I]>0 and ma10[I] or nil; ctx.ma20=ma20[I]>0 and ma20[I] or nil
        ctx.close_arr, ctx.vol_arr, ctx.ma5_arr = cd, vol, ma5
        sig = call()
    end
    print(string.format("    %s %6.2f %6.1f %6.1f %6.1f | %s", od[I], cd[I], ma5[I], ma10[I], ma20[I], sig))
    ::cont::
end
