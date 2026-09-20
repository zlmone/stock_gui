-- BOP 买卖力策略: BOP = SMA((close-open)/(high-low), period); 大于0买入, 小于0卖出。
-- 来源: ea-python BOP指标策略.py; talib.BOP 默认周期=3。
strategy = { name = "买卖力 BOP", desc = "(收盘-开盘)/(最高-最低) 均线, 正买负卖" }
params = { { key="period", label="BOP周期", min=2, max=20,  step=1,  int=true,  value=3 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_mean(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s/(t-f+1) end
local function calcBOP(high, low, open, close, period)
    local mfi = {}; local out = {}
    for i=1,#close do if high[i]>low[i] then mfi[i]=(close[i]-open[i])/(high[i]-low[i]) else mfi[i]=0 end end
    for i=1,#close do out[i]=(i>=period) and arr_mean(mfi,i-period+1,i) or 0 end
    return out
end
function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period then return "HOLD" end
    local bop = calcBOP(ctx.high_arr,ctx.low_arr,ctx.open_arr,ctx.close_arr,P.period)[last]
    if not ctx.holding then
        if bop>0 then return "BUY" end
        return "HOLD"
    else
        if bop<0 then return "SELL" end
        return "HOLD"
    end
end
