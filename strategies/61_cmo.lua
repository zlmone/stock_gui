-- ============================================================
-- 61_cmo.lua -- 钱德动量震荡指标 (移植 tqsdk demo/example/CMO.py, 仅长仓版)
-- 入场(任一):超卖反弹/信号线上穿/零轴上穿(均需价>SMA)。平仓:动量减弱/反向信号/ATR止损/持仓超期。
-- ============================================================
strategy = { name = "CMO", desc = "CMO 多信号, 仅长仓" }
params = {
    { key="period",    label="CMO周期", min=2, max=30, step=1, int=true,  value=6 },
    { key="signal",    label="信号线",  min=2, max=20, step=1, int=true,  value=4 },
    { key="sma_period", label="趋势SMA", min=2, max=40, step=1, int=true,  value=10 },
    { key="atr_period", label="ATR周期", min=5, max=40, step=1, int=true,  value=14 },
    { key="overbought", label="超买阈值", min=10, max=90, step=1,  value=50 },
    { key="overold",    label="超卖阈值", min=-90, max=-10, step=1,  value=-50 },
    { key="atr_mult",   label="ATR止损倍", min=0.5, max=5, step=0.1, value=2 },
    { key="fix_stop",   label="固定止损%", min=0, max=5,  step=0.1, value=0.8 },
    { key="tp",         label="止盈%",     min=0, max=5,  step=0.1, value=1.5 },
    { key="max_days",   label="最大持仓日", min=1, max=40, step=1, int=true,  value=10 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function ma(arr, period, last)
    if last-1 < period then return 0 end
    local s=0
    for j=last-period+1,last do local v=arr[j]; if v then s=s+v end end
    return s/period
end

local function atr(hi, lo, cl, last, period)
    if last < period+1 then return 0 end
    local s=0
    for j=last-period+1,last do local pc=cl[j-1]
        s=s+math.max(hi[j]-lo[j], math.abs(hi[j]-pc), math.abs(lo[j]-pc)) end
    return s/period
end

local function cmo_series(arr, period, upto)
    local out = {}
    for i=1,upto do
        if i-1 < period then out[i]=0
        else
            local up=0; local dn=0
            for j=i-period+1,i do local d=arr[j]-arr[j-1]; if d>0 then up=up+d else dn=dn-d end end
            out[i]=(up+dn==0) and 0 or 100*(up-dn)/(up+dn)
        end
    end
    return out
end

local function roll(arr, period, upto)
    local out = {}
    for i=1,upto do
        if i-1 < period then out[i]=arr[i] else
            local s=0; for j=i-period+1,i do local v=arr[j]; if v then s=s+v end end out[i]=s/period end
    end
    return out
end

function on_bar(ctx)
    local last=ctx.i+1
    local cl=ctx.close_arr; local hi=ctx.high_arr; local lo=ctx.low_arr
    if last<P.period+P.signal+P.sma_period+10 then return "HOLD" end
    local cm=cmo_series(cl, P.period, last)
    local cs=roll(cm, P.signal, last)
    local cur_cmo=cm[last] or 0; local prev_cmo=cm[last-1] or 0
    local cur_sig=cs[last] or 0; local prev_sig=cs[last-1] or 0
    local slope = cm[last] - (cm[last-2] or 0)
    local sma=ma(cl, P.sma_period, last)
    if ctx.holding then
        if ctx.days>=P.max_days then return "SELL" end
        local n=atr(hi,lo,cl,last,P.atr_period)
        if ctx.close <= ctx.buy_price - P.atr_mult*n then return "SELL" end
        if prev_cmo>prev_sig and cur_cmo<cur_sig and cur_cmo>P.overbought then return "SELL" end
        if (prev_cmo>P.overbought and cur_cmo<P.overbought and slope<0) or
           (prev_cmo>prev_sig and cur_cmo<cur_sig and ctx.close<sma) or
           (prev_cmo>0 and cur_cmo<0 and prev_cmo>10) then return "SELL" end
        return "HOLD"
    else
        local s1 = prev_cmo<P.overold and cur_cmo>P.overold and ctx.close>sma and slope>0
        local s2 = prev_cmo<prev_sig and cur_cmo>cur_sig and ctx.close>sma and cur_cmo>-30
        local s3 = prev_cmo<0 and cur_cmo>0 and ctx.close>sma and prev_cmo<-10
        if s1 or s2 or s3 then
            ctx.stop_loss = ctx.close - P.atr_mult*atr(hi,lo,cl,last,P.atr_period)
            ctx.take_profit = ctx.close*(1+P.tp/100)
            return "BUY"
        end
        return "HOLD"
    end
end
