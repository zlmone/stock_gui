-- ATR 指标策略: 前一根收盘 +/- ATR 倍数作轨道, 突破买入/跌破卖出。
-- 来源: ea-python ATR-指标策略.py; ATR(5) 用 Wilder 平滑。
-- 数组约定: close_arr 1 索引, 当前根 ctx.i 对应 ctx.close_arr[ctx.i+1] 为今日收盘。
strategy = { name = "ATR 突破", desc = "前日收盘 +/- ATR 倍数, 突破上轨买/跌破下轨卖" }
params = {
    { key="atr_period",  label="ATR周期",        min=3, max=20,  step=1,  int=true,  value=5 },
    { key="long_multi",   label="多头ATR倍数",     min=0, max=5,  step=0.1,  value=0.2 },
    { key="short_multi",  label="空头ATR倍数",     min=0, max=5,  step=0.1,  value=0.3 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function arr_sum(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s end

-- ATR (Wilder 平滑): 1 索引数组
local function calcATR(high, low, close, period)
    local tr = {}; local atr = {}; local sum = 0
    for i = 1, #close do
        if i == 1 then tr[i] = high[i] - low[i]
        else tr[i] = math.max(high[i]-low[i], math.abs(high[i]-close[i-1]), math.abs(low[i]-close[i-1])) end
    end
    for i = 1, #close do
        if i == period + 1 then
            for j = i-period, i do sum = sum + tr[j] end
            atr[i] = sum/period
        else
            atr[i] = (i>period+1) and ((atr[i-1]*(period-1)+tr[i])/period) or 0
        end
    end
    return atr
end

function on_bar(ctx)
    local last = ctx.i + 1
    local p = P.atr_period
    if last < p + 1 or last < 2 then return "HOLD" end
    local prev = ctx.close_arr[last-1]
    local atr = calcATR(ctx.high_arr, ctx.low_arr, ctx.close_arr, p)[last]
    local up   = prev + P.long_multi * atr
    local down = prev - P.short_multi * atr
    if not ctx.holding then
        if ctx.close_arr[last] > up then return "BUY" end
        return "HOLD"
    else
        if ctx.close_arr[last] < down then return "SELL" end
        return "HOLD"
    end
end
