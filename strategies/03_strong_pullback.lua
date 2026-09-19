-- 强势回踩: 近期有涨停 + 回踩MA20附近买入, 止盈 / 破位止损
strategy = { name = "强势回踩", desc = "20日内有涨停, 回踩MA20买入; 止盈/破位止损" }

params = {
    { key="tp",    label="止盈%",           min=3,  max=20,  step=0.5,   value=8 },
    { key="buy_u", label="回踩上界(×MA20)", min=1.0, max=1.05, step=0.005, value=1.015 },
    { key="buy_l", label="回踩下界(×MA20)", min=0.94, max=1.0, step=0.005, value=0.985 },
    { key="sl",    label="破位止损(×MA20)", min=0.90, max=1.0, step=0.005, value=0.97 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    if ctx.ma20 == nil then return "HOLD" end
    if not ctx.holding then
        if ctx.zt20 >= 1
           and ctx.close <= ctx.ma20 * P.buy_u
           and ctx.close >= ctx.ma20 * P.buy_l then
            return "BUY"
        end
    else
        if ctx.gain >= P.tp / 100 then return "SELL" end
        if ctx.close < ctx.ma20 * P.sl then return "SELL" end
    end
    return "HOLD"
end
