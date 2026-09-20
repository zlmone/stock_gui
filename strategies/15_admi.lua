-- ADMI 平均方向运动指标(ADX): ADX>40 趋势走强买入, ADX<20 趋势走弱卖出。
-- 来源: ea-python ADMI指标策略.py; talib.ADX(period=7)。
strategy = { name = "ADMI ADX 趋势", desc = "ADX>40 买, <20 卖" }
params = { { key="period", label="ADX周期", min=2, max=30,  step=1,  int=true,  value=7 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function wsmo(arr, period)
    local out = {}
    local sum = 0
    for i = 1, #arr do out[i] = 0 end
    for i = 1, period do sum = sum + arr[i] end
    out[period] = sum / period
    for i = period+1, #arr do out[i] = (out[i-1]*(period-1) + arr[i]) / period end
    return out
end
local function calcDI(high, low, close, period)
    local n = #close
    local plusDI = {}; local minusDI = {}; local adx = {}; local dx = {}
    local tr = {}; local PDM = {}; local MDM = {}
    for i = 1, n do
        if i == 1 then tr[i] = high[i] - low[i]
        else tr[i] = math.max(high[i]-low[i], math.abs(high[i]-close[i-1]), math.abs(low[i]-close[i-1])) end
    end
    for i = 1, n do PDM[i] = 0; MDM[i] = 0 end
    for i = 2, n do
        local up = high[i]-high[i-1]; local dn = low[i-1]-low[i]
        if up > dn and up > 0 then PDM[i] = up else PDM[i] = 0 end
        if dn > up and dn > 0 then MDM[i] = dn else MDM[i] = 0 end
    end
    local atr = wsmo(tr, period); local pdm = wsmo(PDM, period); local mdm = wsmo(MDM, period)
    for i = 1, n do
        plusDI[i] = (atr[i] > 0) and 100*pdm[i]/atr[i] or 0
        minusDI[i] = (atr[i] > 0) and 100*mdm[i]/atr[i] or 0
        local s = plusDI[i]+minusDI[i]
        local num = math.abs(plusDI[i]-minusDI[i])
        dx[i] = (s > 0) and 100*num/s or 0
    end
    adx = wsmo(dx, period)
    return plusDI, minusDI, adx
end
function on_bar(ctx)
    local last = ctx.i + 1
    if last < P.period then return "HOLD" end
    local plusDI, minusDI, adx = calcDI(ctx.high_arr, ctx.low_arr, ctx.close_arr, P.period)
    if not ctx.holding then
        if adx[last] > 40 then return "BUY" end
        return "HOLD"
    else
        if adx[last] < 20 then return "SELL" end
        return "HOLD"
    end
end
