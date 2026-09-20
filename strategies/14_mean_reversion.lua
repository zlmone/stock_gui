-- 均值回归: 现价跌破 5 日均价*0.9 买入, 突破*1.2 卖出; 净资产跌破初始 0.75 时止损离场且不再买入。
-- 来源: ea-python "均值回归策略.py"; 净值线等价于平台权益乘数(ctx.equity, 初始=1)。
-- 数组约定: close_arr 1 索引, 当前根 ctx.i 对应 bars[i] => close_arr[ctx.i+1] 为今日收盘。
strategy = { name = "均值回归", desc = "跌破5日均价0.9倍买入, 超1.2倍卖出, 跌破净值0.75止损" }

params = {
    { key="window",    label="均价周期",  min=2,  max=30,  step=1,  int=true,  value=5 },
    { key="buy_thr",   label="买入乘数",  min=0.5, max=1,  step=0.01, value=0.9 },
    { key="sell_thr",  label="卖出乘数",  min=1,  max=3,  step=0.01, value=1.2 },
    { key="stop",      label="净值止损线", min=0,  max=1,  step=0.01, value=0.75 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

-- 区间 [from,to] 内均值(跳过0)
local function arr_mean(a, from, to)
    local s, c = 0, 0
    if from < 1 then from = 1 end
    for k = from, to do local v = a[k] or 0; if v > 0 then s = s + v; c = c + 1 end end
    return c > 0 and s / c or 0
end

local stopped = false  -- 文件级本地变量, 跨 bar 保留; 触发净值止损后停止买入
function on_bar(ctx)
    -- 净值止损线(ctx.equity 为当前权益相对初始乘数)
    if ctx.equity < P.stop then
        if ctx.holding then return "SELL" end  -- 强平
        return "HOLD"                          -- 已空仓: 不再买入
    end
    if stopped then return "HOLD" end
    local last = ctx.i + 1
    local ma = arr_mean(ctx.close_arr, last - P.window + 1, last)
    if ma <= 0 then return "HOLD" end
    local buyB  = ma * P.buy_thr
    local sellB = ma * P.sell_thr
    if not ctx.holding then
        if ctx.close_arr[last] < buyB then return "BUY" end   -- 跌破下轨, 抄底
    else
        if ctx.close_arr[last] > sellB then return "SELL" end  -- 突破上轨, 获利了结
    end
    return "HOLD"
end
