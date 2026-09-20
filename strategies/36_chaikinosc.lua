-- Chaikin A/D Oscillator(ADOSC): AD 线的快慢 EMA 差; >0 买入, <0 卖出。
-- 来源: ea-python Chaikin Oscillatorreal指标策略.py; talib.ADOSC(fast=3, slow=10)。
strategy = { name = "Chaikin ADOSC", desc = "AD线 MACD >0 买, <0 卖" }
params = {
    { key="fast_period", label="快EMA", min=1, max=20,  step=1,  int=true,  value=3 },
    { key="slow_period", label="慢EMA", min=2, max=30,  step=1,  int=true,  value=10 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function calcAD(high, low, close, vol)
    local out = {}; local sum = 0
    for i=1,#close do
        if high[i] > low[i] then
            local mfv = ((close[i]-low[i])-(high[i]-close[i])) / (high[i]-low[i])
            sum = sum + mfv * vol[i]
        end
        out[i] = sum
    end
    return out
end
local function calcEMA(arr, period)
    local out={}; local k=2/(period+1)
    out[1]=arr[1]
    for i=2,#arr do out[i]=arr[i]*k+out[i-1]*(1-k) end
    return out
end
function on_bar(ctx)
    local last = ctx.i + 1
    if last < P.slow_period then return "HOLD" end
    local ad = calcAD(ctx.high_arr, ctx.low_arr, ctx.close_arr, ctx.vol_arr)
    local eF = calcEMA(ad, P.fast_period)
    local eS = calcEMA(ad, P.slow_period)
    local adosc = eF[last] - eS[last]
    if not ctx.holding then
        if adosc > 0 then return "BUY" end
        return "HOLD"
    else
        if adosc < 0 then return "SELL" end
        return "HOLD"
    end
end
