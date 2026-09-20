-- 放量突破: 收盘突破近 N 日高点, 且当日成交量放大, 追强势股; 跌破 MA10 / 持满到期 / 止损离场
-- 定位: 量价突破(需放量确认), 与 05 涨停前夜(缩量蓄势)互补 —— 一个等启动, 一个追突破。
-- 数组约定: close_arr / vol_arr 为 1 索引, 当前根 ctx.i 对应 bars[i]; bars[k].close = close_arr[k+1]
strategy = { name = "放量突破", desc = "突破N日高点+放量, 跌破MA10离场" }

params = {
    { key="look",  label="考察天数",  min=10, max=120, step=5,  int=true,  value=40 },
    { key="vol_r", label="放量系数",  min=1.0, max=5,   step=0.1, value=1.5 },
    { key="hold",  label="最长持有日",min=1,  max=60,  step=1,  int=true,  value=20 },
    { key="sl",    label="破位止损%", min=-20, max=-2, step=0.5, value=-6 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function arr_hi(a, from, to)          -- 区间 [from,to] 内最大值
    local hi = 0
    if from < 1 then from = 1 end
    for k = from, to do
        local v = a[k] or 0
        if v > hi then hi = v end
    end
    return hi
end
local function arr_avg(a, from, to)         -- 区间 [from,to] 内均值(跳过0)
    local s, c = 0, 0
    if from < 1 then from = 1 end
    for k = from, to do
        local v = a[k] or 0
        if v > 0 then s = s + v; c = c + 1 end
    end
    return c > 0 and s / c or 0
end

function on_bar(ctx)
    local i = ctx.i
    if i < P.look + 5 then return "HOLD" end
    if not ctx.holding then
        -- 近N日高点(不含今天): bars[i-1..i-look]  => arr[i .. i-look+1]
        local breakout = arr_hi(ctx.close_arr, i - P.look + 1, i)
        if breakout <= 0 then return "HOLD" end
        if ctx.close <= breakout then return "HOLD" end      -- 未突破
        -- 放量: 今日量(arr[i+1]) 大于 近5日均量(arr[i-4..i]) 的 vol_r 倍
        local avg5 = arr_avg(ctx.vol_arr, i - 4, i)
        local todayv = ctx.vol_arr[i + 1] or 0
        if avg5 <= 0 or todayv <= P.vol_r * avg5 then return "HOLD" end
        return "BUY"
    else
        if ctx.days >= P.hold then return "SELL" end
        if ctx.ma10 > 0 and ctx.close < ctx.ma10 then return "SELL" end
        if ctx.gain < P.sl / 100 then return "SELL" end
    end
    return "HOLD"
end
