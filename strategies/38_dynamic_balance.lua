-- 动态平衡 (全仓近似版)。
-- 来源: ea-python 动态平衡策略.py. 原策略把 现金/持仓 维持约 50/50, 当二者偏离净资产超过阈值(=净资产×max_diff)时再平衡回 50/50。
-- 本质是"逢低买/逢高减"的均值回归, 与平台全仓单票模型不符, 故按"价格偏离均线的幅度超阈值"近似再平衡信号。
strategy = { name = "动态平衡(近似)", desc = "价格偏离均线超阈值时逢低买/逢高减(全仓近似)" }
params = {
    { key="sma_window", label="基准均线(日)", min=5, max=60,  step=1,  int=true,  value=20 },
    { key="deviation",  label="偏离阈值",     min=0, max=0.5,  step=0.01,  value=0.05 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_mean(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s/(t-f+1) end
function on_bar(ctx)
    local last = ctx.i + 1
    if last < P.sma_window then return "HOLD" end
    local s = arr_mean(ctx.close_arr, last-P.sma_window+1, last)
    if not ctx.holding then
        if ctx.close_arr[last] < s*(1-P.deviation) then return "BUY" end
        return "HOLD"
    else
        if ctx.close_arr[last] > s*(1+P.deviation) then return "SELL" end
        return "HOLD"
    end
end
