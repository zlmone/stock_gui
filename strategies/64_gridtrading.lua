-- ============================================================
-- 64_gridtrading.lua -- 网格交易 (移植 tqsdk demo/example/gridtrading.py)
-- 以起始价为中心: 多头向下 10 档(每档密度), 空头向上 10 档(每档密度)
-- 价格每下探一档买入一档手数(累积多头), 上冲一档卖出(累积空头);
-- 返回引擎目标持仓 TARGET_VOL, 由引擎负责开平结算与余额校验。
-- ============================================================
strategy = { name = "网格交易", desc = "tqsdk 双向阶梯网格(累积持仓)" }
params = {
    { key="start_price", label="起始价",  min=1, max=1e6, step=1,  value=0 },
    { key="grid_amount", label="网格档数", min=1, max=30,  step=1,  int=true,  value=10 },
    { key="grid_long",   label="多头网格密度%", min=0.1, max=5, step=0.05, value=0.5 },
    { key="grid_short",  label="空头网格密度%", min=0.1, max=5, step=0.05, value=0.5 },
    { key="commission",  label="佣金%",   min=0, max=5,  step=0.001, value=0.02 },
    { key="slippage",    label="滑点%",   min=0, max=5,  step=0.001, value=0.05 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local start_price = 0
local gp_long = {}; local gp_short = {}
local built = false

local function build(first_close)
    if first_close > 0 then start_price = first_close end
    local i = 0
    while i <= P.grid_amount do
        gp_long[i] = start_price * (1 - P.grid_long / 100) ^ i
        gp_short[i] = start_price * (1 + P.grid_short / 100) ^ i
        i = i + 1
    end
    built = true
end

function on_bar(ctx)
    if not built then build(ctx.close_arr[ctx.i + 1]) end
    local price = ctx.close
    local L = 0
    local i = 1
    while i <= P.grid_amount do
        if gp_long[i] >= price then L = i end
        i = i + 1
    end
    local S = 0
    i = 1
    while i <= P.grid_amount do
        if gp_short[i] <= price then S = i end
        i = i + 1
    end
    if L > 0 then TARGET_VOL = L else TARGET_VOL = -S end
end
