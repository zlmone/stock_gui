-- 价值平均定投 (全仓近似版)。
-- 来源: ea-python 价值平均定投策略.py. 原策略每期把 持仓总值 提高固定金额(分批建仓/长期定投)。
-- 本质是长期 accumulate, 与平台全仓模型不符; 近似为"逢低建仓、达到目标收益退出、循环建仓"。
strategy = { name = "价值平均定投(近似)", desc = "逢低建仓、达目标收益退出(全仓近似)" }
params = {
    { key="sma_window", label="建仓均线(日)", min=5, max=60,  step=1,  int=true,  value=20 },
    { key="win_target", label="目标收益%",    min=1, max=300,  step=1,  value=30 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_mean(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s/(t-f+1) end
function on_bar(ctx)
    local last = ctx.i + 1
    if last < P.sma_window then return "HOLD" end
    local s = arr_mean(ctx.close_arr, last-P.sma_window+1, last)
    if not ctx.holding then
        if ctx.close_arr[last] < s then return "BUY" end
        return "HOLD"
    else
        if (ctx.gain*100) >= P.win_target then return "SELL" end
        return "HOLD"
    end
end
