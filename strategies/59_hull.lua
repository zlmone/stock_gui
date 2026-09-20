-- ============================================================
-- 59_hull.lua -- Hull 移动平均线 (移植 tqsdk demo/example/hull.py, 仅长仓版)
-- 仅长仓: 短HMA上穿长HMA 且 收>长HMA 买入; 短HMA下穿/跌破跟踪止损(短HMA-2·ATR)/止盈3%平仓。
-- ============================================================
strategy = { name = "Hull", desc = "HMA 短穿长 + ATR 跟踪止损, 仅长仓" }
params = {
    { key="short",    label="短HMA周期", min=2,  max=30,  step=1,  int=true,  value=5 },
    { key="long",     label="长HMA周期", min=3,  max=60,  step=1,  int=true,  value=30 },
    { key="atr_period", label="ATR周期",  min=5,  max=40,  step=1,  int=true,  value=14 },
    { key="atr_mult", label="ATR止损倍",  min=0.5,max=5,   step=0.1, value=2 },
    { key="take_profit_pct", label="止盈%", min=1, max=10,  step=0.1, value=3 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function wma(arr, period, last)
    local sumw=period*(period+1)/2; local s=0
    for k=1,period do local v=arr[last-period+k]; if v then s=s+k*v end end
    return s/sumw
end

local function hma(arr, period, upto)
    local half=period//2; local sq=math.floor(math.sqrt(period)); local raw={}
    for j=1,upto do
        if period<3 then raw[j]=wma(arr,period,j) else raw[j]=2*wma(arr,half,j)-wma(arr,period,j) end
    end
    local sqw=sq*(sq+1)/2; local out={}
    for j=1,upto do local s=0
        for k=1,sq do local v=raw[j-sq+k]; if v then s=s+k*v end end
        out[j]=sq<3 and wma(raw,sq,j) or s/sqw end
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
    if last<P.long+10 then return "HOLD" end
    local sh=hma(cl, P.short, last); local lh=hma(cl, P.long, last)
    local psh=sh[last-1]; local csh=sh[last]
    local plh=lh[last-1]; local clh=lh[last]
    if ctx.holding then
        local n=atr(hi,lo,cl,last,P.atr_period)
        local stop=math.max(ctx.buy_price-P.atr_mult*n, csh-P.atr_mult*n)
        if ctx.close<=stop then return "SELL" end
        if ctx.close>=ctx.buy_price*(1+P.take_profit_pct/100) then return "SELL" end
        if psh>=plh and csh<clh then return "SELL" end
        return "HOLD"
    else
        if psh<=plh and csh>clh and ctx.close>clh then return "BUY" end
        return "HOLD"
    end
end
