-- BOLL 轨道突破: 收盘突破 2 倍标准差上轨买入, 跌破下轨卖出。
-- 来源: ea-python "BOLL指标策略.py"; talib.BBANDS(period=14, nbdev=2, SMA)。
-- 数组约定: close_arr 1 索引, 当前根 ctx.i 对应 bars[i] => close_arr[ctx.i+1] 为今日收盘。
strategy = { name = "BOLL突破", desc = "突破2倍标准差上轨买入, 下轨卖出" }

params = {
    { key="period",    label="周期",      min=5,  max=60,  step=1,  int=true,  value=14 },
    { key="nbdev",     label="标准差倍数", min=0.5, max=4,  step=0.1, value=2 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    local last = ctx.i + 1                -- 当前根(1 索引)
    local p = P.period
    if last < p then return "HOLD" end
    local s, n = 0, 0
    for k = last - p + 1, last do local v = ctx.close_arr[k] or 0; if v > 0 then s = s + v; n = n + 1 end end
    if n == 0 then return "HOLD" end
    local mean = s / n
    local ss = 0
    for k = last - p + 1, last do local v = ctx.close_arr[k] or 0; ss = ss + (v - mean) * (v - mean) end
    local std = math.sqrt(ss / n)          -- talib BBANDS 用总体标准差(除以 N)
    local upper = mean + P.nbdev * std
    local lower = mean - P.nbdev * std
    local close = ctx.close_arr[last]
    if not ctx.holding then
        if close > upper then return "BUY" end
    else
        if close < lower then return "SELL" end
    end
    return "HOLD"
end
