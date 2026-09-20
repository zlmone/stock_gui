-- EMA 指数均线交叉: 快线向上穿越慢线买入, 向下穿越(或回撤达止损线)卖出。
-- 来源: ea-python EMA指标策略.py; EMA5/EMA20, 回撤止损10%。
-- 数组约定: close_arr 1 索引; equity 为当前净值(起点 1.0); 回撤=（1-equity/峰值）*100。
strategy = { name = "EMA 交叉回撤止损", desc = "EMA快线交叉买入, 回撤达10%止损" }
params = {
    { key="fast_window",  label="快线周期", min=2, max=30,  step=1,  int=true,  value=5 },
    { key="slow_window",  label="慢线周期", min=5, max=60,  step=1,  int=true,  value=20 },
    { key="stop_loss",    label="回撤止损%", min=1, max=50,  step=1,  value=10 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function calcEMA(arr, period)
    local out={}; local k=2/(period+1)
    out[1]=arr[1]
    for i=2,#arr do out[i]=arr[i]*k+out[i-1]*(1-k) end
    return out
end
local max_equity  -- 净值峰值(文件级, 跨根保持)
function on_bar(ctx)
    local last=ctx.i+1
    if last<P.slow_window+1 then return "HOLD" end
    if max_equity == nil or ctx.equity>max_equity then max_equity=ctx.equity end
    local emaF=calcEMA(ctx.close_arr,P.fast_window)[last]
    local emaS=calcEMA(ctx.close_arr,P.slow_window)[last]
    local emaFp=(last>1) and calcEMA(ctx.close_arr,P.fast_window)[last-1] or 0
    local emaSp=(last>1) and calcEMA(ctx.close_arr,P.slow_window)[last-1] or 0
    local drawdown=(1-(ctx.equity/max_equity))*100
    if not ctx.holding then
        if emaFp<=emaSp and emaF>emaS then return "BUY" end
        return "HOLD"
    else
        if drawdown>P.stop_loss then return "SELL" end
        if emaFp>=emaSp and emaF<emaS then return "SELL" end
        return "HOLD"
    end
end
