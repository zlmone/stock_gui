-- MOM 动量策略: close 与 n 根前的收盘之差; 大于0买入, 小于0卖出。
-- 来源: ea-python MOM指标策略.py; talib.MOM(period=6)。
strategy = { name = "动量 MOM", desc = "当前收盘较 n 根前的变动, 正买负卖" }
params = { { key="period", label="动量周期", min=2, max=30,  step=1,  int=true,  value=6 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
function on_bar(ctx)
    local last=ctx.i+1
    if last<=P.period then return "HOLD" end
    local mom = ctx.close_arr[last] - ctx.close_arr[last-P.period]
    if not ctx.holding then
        if mom>0 then return "BUY" end
        return "HOLD"
    else
        if mom<0 then return "SELL" end
        return "HOLD"
    end
end
