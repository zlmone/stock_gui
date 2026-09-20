-- 简单双均线(交叉): 近 5 日收盘均价上穿近 20 日收盘均价全仓买入, 下穿则全仓卖出。
-- 来源: ea-python "简单双均线策略.py" (WeBack / WeQuant 框架: initialize+handle_data)。
-- 平台映射: handle_data->on_bar(ctx); get_price(count)->ctx.close_arr; 全单->BUY/SELL。
-- 数组约定: ctx.close_arr 为 1 索引, 当前根 ctx.i 对应 bars[i] => close_arr[ctx.i+1] 为今日收盘。
strategy = { name = "简单双均线交叉", desc = "MA短线上穿长均线买入, 下穿卖出" }

params = {
    { key="window_short", label="短均线天数", min=2,  max=30,  step=1,  int=true,  value=5 },
    { key="window_long",  label="长均线天数", min=5,  max=120, step=1,  int=true,  value=20 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

-- 区间 [from,to] 内均值(跳过0); 缺省<1 则从1 起算
local function arr_mean(a, from, to)
    local s, c = 0, 0
    if from < 1 then from = 1 end
    for k = from, to do local v = a[k] or 0; if v > 0 then s = s + v; c = c + 1 end end
    return c > 0 and s / c or 0
end

function on_bar(ctx)
    local last = ctx.i + 1                 -- 当前根(1 索引)
    local wS, wL = P.window_short, P.window_long
    local maS = arr_mean(ctx.close_arr, last - wS + 1, last)
    local maL = arr_mean(ctx.close_arr, last - wL + 1, last)
    if maS <= 0 or maL <= 0 then return "HOLD" end   -- 数据不足(早段), 等待
    if not ctx.holding then
        if maS > maL then return "BUY" end            -- 短线上穿长均线(阈值=0)
    else
        if maS < maL then return "SELL" end           -- 线下穿长均线
    end
    return "HOLD"
end
