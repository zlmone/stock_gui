-- ============================================================
-- 50_qustick.lua -- Qstick 零轴穿越 (移植 tqsdk demo/example/Qstick.py, 仅长仓版)
-- Qstick=(close-open)的period均值; 仅长仓: 负上穿0且价>SMA 买入, 正下穿0/价止损/时间止损平仓。
-- ============================================================
strategy = { name = "Qstick", desc = "Qstick 零轴穿越, 仅长仓做多" }
params = {
    { key="period",   label="Qstick周期", min=2, max=40,  step=1,  int=true,  value=10 },
    { key="sma",      label="价格SMA",    min=2, max=40,  step=1,  int=true,  value=8 },
    { key="max_days", label="最大持仓日", min=1, max=60,  step=1,  int=true,  value=5 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function diffma(cl, op, period, last)
    if last-1 < period then return 0 end
    local sq=0; local so=0
    for j=last-period+1,last do local v=cl[j]; if v then sq=sq+v end; local w=op[j]; if w then so=so+w end end
    return sq/period - so/period
end

function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period+math.max(P.sma,3) then return "HOLD" end
    local cl=ctx.close_arr; local op=ctx.open_arr
    local q_prev=diffma(cl, op, P.period, last-1)
    local q_cur =diffma(cl, op, P.period, last)
    local sma = (last-1>=P.sma) and (function() local s=0; for j=last-P.sma+1,last do local v=cl[j]; if v then s=s+v end end return s/P.sma end)() or 0
    if ctx.holding then
        if q_prev>0 and q_cur<0 then return "SELL" end
        if ctx.days>=P.max_days then return "SELL" end
        return "HOLD"
    else
        if q_prev<0 and q_cur>0 and ctx.close>sma then return "BUY" end
        return "HOLD"
    end
end
