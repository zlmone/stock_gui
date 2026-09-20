-- CMO  Chaikin 动量指标(类 RSI, Wilder 平滑, 带符号): CMO<-30 买入, >70 卖出。
-- 来源: ea-python CMO指标策略.py; talib.CMO(period=30)。
strategy = { name = "CMO 动量", desc = "CMO<-30 买, >70 卖" }
params = {
    { key="period",  label="CMO周期", min=2, max=60,  step=1,  int=true,  value=30 },
    { key="over_sell", label="超卖线", min=-100, max=0,  step=1,  value=-30 },
    { key="over_buy",  label="超买线", min=0, max=100,  step=1,  value=70 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function calcCMO(close, period)
    local out={}; local n=#close
    for i=1,n do out[i]=0 end
    local avgG=0; local avgL=0
    for i=2,n do
        local d=close[i]-close[i-1]; local g=0; local l=0
        if d>0 then g=d else l=-d end
        if i<=period+1 then avgG=avgG+g/period; avgL=avgL+l/period
        else avgG=(avgG*(period-1)+g)/period; avgL=(avgL*(period-1)+l)/period end
        if i>=period+1 then out[i]=(avgG+avgL>0) and 100*(avgG-avgL)/(avgG+avgL) or 0 end
    end
    return out
end
function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period+1 then return "HOLD" end
    local cmo=calcCMO(ctx.close_arr,P.period)[last]
    if not ctx.holding then
        if cmo<P.over_sell then return "BUY" end
        return "HOLD"
    else
        if cmo>P.over_buy then return "SELL" end
        return "HOLD"
    end
end
