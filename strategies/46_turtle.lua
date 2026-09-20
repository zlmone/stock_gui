-- ============================================================
-- 46_turtle.lua -- 海龟策略 (移植 tqsdk demo/example/turtle.py, 仅长仓简化版)
-- 唐奇安20日通道突破做多; 2N(ATR)止损; 跌破10日唐奇安下轨离场。加仓/做空省略。
-- ============================================================
strategy = { name = "海龟", desc = "唐奇安通道突破+2N止损, 仅长仓" }
params = {
    { key="open",     label="开仓唐奇安日", min=5,  max=60,  step=1,  int=true,  value=20 },
    { key="stop",     label="止盈唐奇安日", min=2,  max=40,  step=1,  int=true,  value=10 },
    { key="atr_period", label="ATR周期",    min=5,  max=60,  step=1,  int=true,  value=20 },
    { key="atr_stop", label="止损N倍",      min=0.5,max=5,   step=0.1, value=2 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function atr(hi, lo, cl, last, period)
    if last < period+1 then return 0 end
    local s=0
    for j=last-period+1,last do local pc=cl[j-1]
        local tr=math.max(hi[j]-lo[j], math.abs(hi[j]-pc), math.abs(lo[j]-pc)); s=s+tr end
    return s/period
end

function on_bar(ctx)
    local last=ctx.i+1
    if last<P.open+1 then return "HOLD" end
    local hi=ctx.high_arr; local lo=ctx.low_arr; local cl=ctx.close_arr
    local hh=-1e18; local ll=1e18
    for j=last-P.open,last-1 do if hi[j]>hh then hh=hi[j] end if lo[j]<ll then ll=lo[j] end end
    if ctx.holding then
        local n=atr(hi,lo,cl,last,P.atr_period); local sl=ctx.buy_price-P.atr_stop*n
        if ctx.close<=sl then return "SELL" end
        local ll10=1e18; for j=last-P.stop,last-1 do if lo[j]<ll10 then ll10=lo[j] end end
        if ctx.close<=ll10 then return "SELL" end
        return "HOLD"
    else
        if ctx.close>hh then return "BUY" end
        return "HOLD"
    end
end
