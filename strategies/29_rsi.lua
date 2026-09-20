-- RSI 相对强弱: 跌破超卖线(20)买入, 突破超买线(80)卖出。
-- 来源: ea-python RSI指标策略.py; talib.RSI(period=21, Wilder 平滑)。
strategy = { name = "RSI 相对强弱", desc = "低于超卖线买, 高于超买线卖" }
params = {
    { key="period", label="RSI周期", min=2, max=30,  step=1,  int=true,  value=21 },
    { key="over_sell", label="超卖线", min=0, max=50,  step=1,  value=20 },
    { key="over_buy", label="超买线", min=50, max=100,  step=1,  value=80 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function calcRSI(close, period)
    local out={}; local n=#close
    for i=1,n do out[i]=50 end
    local avgG=0; local avgL=0
    for i=2,n do
        local d=close[i]-close[i-1]; local g=0; local l=0
        if d>0 then g=d else l=-d end
        if i<=period+1 then avgG=avgG+g/period; avgL=avgL+l/period
        else avgG=(avgG*(period-1)+g)/period; avgL=(avgL*(period-1)+l)/period end
        if i>=period+1 then out[i]=(avgL>0) and (100-100/(1+avgG/avgL)) or 100 end
    end
    return out
end
function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period+1 then return "HOLD" end
    local rsi=calcRSI(ctx.close_arr,P.period)[last]
    if not ctx.holding then
        if rsi<P.over_sell then return "BUY" end
        return "HOLD"
    else
        if rsi>P.over_buy then return "SELL" end
        return "HOLD"
    end
end
