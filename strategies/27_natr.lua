-- NATR 指标策略: 前一根收盘 +/- 0.1·NATR(10) 作轨道, 突破买入/跌破卖出。
-- 来源: ea-python NATR指标策略.py; NATR = ATR*100/close。
strategy = { name = "NATR 突破", desc = "前日收盘 +/- 0.1·NATR 轨道, 突破买/跌破卖" }
params = {
    { key="natr_period", label="NATR周期", min=3, max=20,  step=1,  int=true,  value=10 },
    { key="long_multi",   label="多头NATR倍数", min=0, max=5,  step=0.1,  value=0.1 },
    { key="short_multi",  label="空头NATR倍数", min=0, max=5,  step=0.1,  value=0.1 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_sum(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s end
local function calcATR(high, low, close, period)
    local tr={}; local atr={}; local sum=0
    for i=1,#close do
        if i==1 then tr[i]=high[i]-low[i]
        else tr[i]=math.max(high[i]-low[i], math.abs(high[i]-close[i-1]), math.abs(low[i]-close[i-1])) end
    end
    for i=1,#close do
        if i==period+1 then for j=i-period,i do sum=sum+tr[j] end; atr[i]=sum/period
        else atr[i]=(i>period+1) and ((atr[i-1]*(period-1)+tr[i])/period) or 0 end
    end
    return atr
end
function on_bar(ctx)
    local last=ctx.i+1; local p=P.natr_period
    if last<p+1 or last<2 then return "HOLD" end
    local prev=ctx.close_arr[last-1]
    local atr=calcATR(ctx.high_arr,ctx.low_arr,ctx.close_arr,p)[last]
    local natr=atr*100/ctx.close_arr[last]
    local up=prev+P.long_multi*natr; local down=prev-P.short_multi*natr
    if not ctx.holding then
        if ctx.close_arr[last]>up then return "BUY" end
        return "HOLD"
    else
        if ctx.close_arr[last]<down then return "SELL" end
        return "HOLD"
    end
end
