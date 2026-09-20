-- ============================================================
-- 52_pivot_point.lua -- 枢轴点 (移植 tqsdk demo/example/pivot_point.py, 仅长仓版)
-- 用前一K线的HLC定枢轴与S1; 仅长仓: 跌破S1 买入, 回到枢轴或破止损平仓。
-- ============================================================
strategy = { name = "枢轴点", desc = "枢轴点支撑买入, 仅长仓" }
params = {
    { key="stop_line", label="止损点数", min=1, max=500, step=1, value=100 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    local last=ctx.i+1
    if last<2 then return "HOLD" end
    local ph=ctx.high_arr[last-1]; local pl=ctx.low_arr[last-1]; local pc=ctx.close_arr[last-1]
    local pivot=(ph+pl+pc)/3
    local s1=2*pivot-ph
    if ctx.holding then
        if ctx.close>=pivot then return "SELL" end
        if ctx.close<=ctx.buy_price-P.stop_line then return "SELL" end
        return "HOLD"
    else
        if ctx.close<s1 then return "BUY" end
        return "HOLD"
    end
end
