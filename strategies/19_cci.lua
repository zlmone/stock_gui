-- CCI 商品通道指标: 穿越 +100 买入, 穿越 -100 卖出。
-- 来源: ea-python CCI指标策略.py; talib.CCI(period=14)。
strategy = { name = "CCI 通道", desc = "穿越+100买, 穿越-100卖" }
params = { { key="period", label="CCI周期", min=2, max=30,  step=1,  int=true,  value=14 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_mean(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s/(t-f+1) end
local function calcCCI(high, low, close, period)
    local tp={}; local n=#close
    for i=1,n do tp[i]=(high[i]+low[i]+close[i])/3 end
    local out={}
    for i=1,n do
        if i>=period then
            local ma=arr_mean(tp,i-period+1,i)
            local md=0 for j=i-period+1,i do md=md+math.abs(tp[j]-ma) end md=md/period
            out[i]=(md>0) and (tp[i]-ma)/(0.015*md) or 0
        else out[i]=0 end
    end
    return out
end
function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period then return "HOLD" end
    local cci=calcCCI(ctx.high_arr,ctx.low_arr,ctx.close_arr,P.period)[last]
    local ccip=(last>1) and calcCCI(ctx.high_arr,ctx.low_arr,ctx.close_arr,P.period)[last-1] or 0
    if not ctx.holding then
        if cci>100 and ccip<=100 then return "BUY" end
        return "HOLD"
    else
        if cci<-100 and ccip>=-100 then return "SELL" end
        return "HOLD"
    end
end
