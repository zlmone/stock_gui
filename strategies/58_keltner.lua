-- ============================================================
-- 61_keltner.lua -- 凯尔特纳通道 (移植 tqsdk demo/example/Keltner_Channel.py, 仅长仓版)
-- 仅长仓: 收>上轨且 短EMA>EMA(趋势向上) 买入; 跌破EMA/止损平仓。
-- ============================================================
strategy = { name = "凯尔特纳", desc = "Keltner 通道突破, 仅长仓" }
params = {
    { key="ema",      label="EMA周期", min=2, max=40,  step=1,  int=true,  value=8 },
    { key="atr_period", label="ATR周期", min=2, max=40,  step=1,  int=true,  value=7 },
    { key="atr_mult", label="ATR乘数", min=0.1, max=5, step=0.05, value=1.5 },
    { key="short",    label="确认EMA", min=2, max=30,  step=1,  int=true,  value=5 },
    { key="stop_pct", label="止损%",   min=0, max=10,  step=0.1, value=0.8 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function ema(arr, period, upto)
    local alpha=2/(period+1); local out={}; local prev=nil
    for i=1,upto do local v=arr[i]; if v then if prev==nil then prev=v else prev=alpha*v+(1-alpha)*prev end out[i]=prev end end
    return out
end

local function atr(hi, lo, cl, last, period)
    if last < period+1 then return 0 end
    local s=0
    for j=last-period+1,last do local pc=cl[j-1]
        s=s+math.max(hi[j]-lo[j], math.abs(hi[j]-pc), math.abs(lo[j]-pc)) end
    return s/period
end

function on_bar(ctx)
    local last=ctx.i+1
    local cl=ctx.close_arr; local hi=ctx.high_arr; local lo=ctx.low_arr
    if last<P.ema+P.atr_period+5 then return "HOLD" end
    local mid=ema(cl, P.ema, last); local sh=ema(cl, P.short, last)
    local atrv=atr(hi,lo,cl,last,P.atr_period)
    local trend = sh[last] > mid[last] and 1 or -1
    if ctx.holding then
        local sl=ctx.buy_price*(1-P.stop_pct/100)
        if ctx.close<=mid[last] or ctx.close<=sl then return "SELL" end
        return "HOLD"
    else
        if ctx.close>mid[last]+P.atr_mult*atrv and trend>0 then return "BUY" end
        return "HOLD"
    end
end
