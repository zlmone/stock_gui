-- AR 人气指标: AR = Σ(high-open)/Σ(open-low)·100 (周期26); <超卖线70 买入, >超买线150 卖出。
-- 来源: ea-python AR指标策略.py。
strategy = { name = "AR 人气", desc = "AR<70 买, >150 卖" }
params = {
    { key="period", label="AR周期", min=5, max=60,  step=1,  int=true,  value=26 },
    { key="over_sell", label="超卖线", min=0, max=150,  step=1,  value=70 },
    { key="over_buy", label="超买线", min=100, max=300,  step=1,  value=150 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function calcAR(high, open, low, from, to)
    local num = 0; local den = 0
    for i=from,to do num = num + (high[i]-open[i]); den = den + (open[i]-low[i]) end
    return (den~=0) and num/den*100 or 0
end
function on_bar(ctx)
    local last = ctx.i + 1
    if last<P.period then return "HOLD" end
    local ar = calcAR(ctx.high_arr, ctx.open_arr, ctx.low_arr, last-P.period+1, last)
    if not ctx.holding then
        if ar<P.over_sell then return "BUY" end
        return "HOLD"
    else
        if ar>P.over_buy then return "SELL" end
        return "HOLD"
    end
end
