-- TEMA 三重指数均线: 收盘在 TEMA 上方买入, 下方卖出。
-- 来源: ea-python TEMA指标策略.py; talib.TEMA(period=60)。
strategy = { name = "TEMA 三重均线", desc = "收盘在 TEMA 上方买, 下方卖" }
params = { { key="period", label="TEMA周期", min=5, max=80,  step=1,  int=true,  value=60 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function calcEMA(arr, period)
    local out={}; local k=2/(period+1)
    out[1]=arr[1]
    for i=2,#arr do out[i]=arr[i]*k+out[i-1]*(1-k) end
    return out
end
local function calcTEMA(close, period)
    local e1=calcEMA(close,period); local e2=calcEMA(e1,period); local e3=calcEMA(e2,period)
    local out={}
    for i=1,#close do out[i]=3*e1[i]-3*e2[i]+e3[i] end
    return out
end
function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period then return "HOLD" end
    local tema=calcTEMA(ctx.close_arr,P.period)[last]
    if not ctx.holding then
        if ctx.close_arr[last]>tema then return "BUY" end
        return "HOLD"
    else
        if ctx.close_arr[last]<tema then return "SELL" end
        return "HOLD"
    end
end
