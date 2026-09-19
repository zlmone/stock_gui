-- 双均线金叉: MA5上穿MA20买入, 死叉卖出 (无参数)
strategy = { name = "双均线金叉", desc = "MA5上穿MA20买入, MA5下穿MA20卖出" }

function on_bar(ctx)
    if ctx.ma5 == nil or ctx.ma20 == nil or ctx.ma5_prev == nil then
        return "HOLD"
    end
    if not ctx.holding then
        if ctx.ma5 > ctx.ma20 and ctx.ma5_prev <= ctx.ma20_prev then
            return "BUY"
        end
    else
        if ctx.ma5 < ctx.ma20 then return "SELL" end
    end
    return "HOLD"
end
