-- Chaikin 资金流向(AD): AD 向上(>0)时买入, 向下(<0)时卖出。
-- 来源: ea-python Chaikin指标.py; talib.AD 累积分布线。
strategy = { name = "Chaikin AD 资金流", desc = "AD>0 买, <0 卖" }
params = { { key="period", label="AD回看", min=1, max=60,  step=1,  int=true,  value=60 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function calcAD(high, low, close, vol)
    local out = {}; local sum = 0
    for i = 1, #close do
        if high[i] > low[i] then
            local mfv = ((close[i]-low[i])-(high[i]-close[i])) / (high[i]-low[i])
            sum = sum + mfv * vol[i]
        end
        out[i] = sum
    end
    return out
end
function on_bar(ctx)
    local last = ctx.i + 1
    if last < P.period then return "HOLD" end
    -- 仅取基准价附近一段窗口以匹配"近 N 日"口径
    local s = last - P.period + 1
    local vol = ctx.vol_arr
    local hi = {}; local lo = {}; local cl = {}
    for i = s, last do hi[#hi+1]=ctx.high_arr[i]; lo[#lo+1]=ctx.low_arr[i]; cl[#cl+1]=ctx.close_arr[i]; vol[#vol+1]=vol[i] end
    local ad = calcAD(hi, lo, cl, vol)
    local adcur = ad[#ad]
    if not ctx.holding then
        if adcur > 0 then return "BUY" end
        return "HOLD"
    else
        if adcur < 0 then return "SELL" end
        return "HOLD"
    end
end
