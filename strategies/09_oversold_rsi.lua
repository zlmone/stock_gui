-- RSI 超卖反弹: RSI(14) 跌入超卖(<30)买入, 回到正常/超买(>75)或持满到期/小止损卖出
-- 定位: 均值回归(逆势抄底), 与其它趋势类策略方向相反, 用于分散风险。
-- 数组约定: close_arr 为 1 索引, 当前根 ctx.i 对应 bars[i] => close_arr[i+1] 为今日收盘
strategy = { name = "RSI超卖反弹", desc = "RSI<30超卖买入, RSI>75或到期卖出" }

params = {
    { key="rsi_lo", label="超卖线",    min=15, max=40,  step=1,  value=30 },
    { key="rsi_hi", label="超买线",    min=60, max=90,  step=1,  value=75 },
    { key="hold",   label="最长持有日",min=1,  max=30,  step=1,  int=true,  value=8 },
    { key="stop",   label="固定止损%", min=-20, max=-2, step=0.5, value=-7 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

-- lastIdx 为目标根(1索引), period 周期
local function rsi(ctx, lastIdx, period)
    if lastIdx <= period then return 50 end
    local gain, loss = 0, 0
    for k = lastIdx - period, lastIdx - 1 do
        local cur = ctx.close_arr[k] or 0
        local prv = ctx.close_arr[k - 1] or 0
        local d = cur - prv
        if d > 0 then gain = gain + d else loss = loss - d end
    end
    if loss == 0 then return 100 end
    return 100 - 100 / (1 + gain / loss)
end

function on_bar(ctx)
    local r = rsi(ctx, ctx.i + 1, 14)
    if not ctx.holding then
        if r < P.rsi_lo then return "BUY" end
    else
        if r > P.rsi_hi then return "SELL" end
        if ctx.days >= P.hold then return "SELL" end
        if ctx.gain < P.stop / 100 then return "SELL" end
    end
    return "HOLD"
end
