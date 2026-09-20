-- 价值平均定卖 (全仓近似版)。
-- 来源: ea-python 价值平均定卖策略.py. 原策略每期把 持仓占比 降低固定比例(逐步减仓/定时退出)。
-- 本质是长期多头分阶段平仓, 与平台全仓模型不符; 近似为"逢低建仓、持有到定时退出"。
strategy = { name = "价值平均定卖(近似)", desc = "逢低建仓、持有到定时退出(全仓近似)" }
params = {
    { key="sma_window", label="建仓均线(日)", min=5, max=60,  step=1,  int=true,  value=20 },
    { key="schedule",   label="持有周期(日)", min=5, max=250,  step=1,  int=true,  value=60 },
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
        if ctx.days >= P.schedule then return "SELL" end
        return "HOLD"
    end
end
