-- ROC 变化率策略: ROC = (close - close[n])/close[n]·100; 大于0买入, 小于0卖出。
-- 来源: ea-python ROC指标策略.py; talib.ROC(period=14)。
strategy = { name = "ROC 变化率", desc = "收盘较 n 根前的变化率, 正买负卖" }
params = { { key="period", label="ROC周期", min=2, max=30,  step=1,  int=true,  value=14 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
function on_bar(ctx)
    local last=ctx.i+1
    if last<=P.period then return "HOLD" end
    local roc = (ctx.close_arr[last] - ctx.close_arr[last-P.period]) / ctx.close_arr[last-P.period] * 100
    if not ctx.holding then
        if roc>0 then return "BUY" end
        return "HOLD"
    else
        if roc<0 then return "SELL" end
        return "HOLD"
    end
end
