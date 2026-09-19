-- 低位企稳: 60日低位 + 站上MA5买入, 止盈 / 止损
strategy = { name = "低位企稳", desc = "60日低位 且 收盘上穿MA5买入; 止盈/止损" }

params = {
    { key="pos_max", label="位置上限%", min=10, max=70, step=5,   value=35 },
    { key="tp",      label="止盈%",    min=2,  max=15, step=0.5, value=6 },
    { key="sl",      label="止损%",    min=-15, max=-2, step=0.5, value=-5 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    if ctx.ma5 == nil or ctx.pos60 == nil then return "HOLD" end
    if not ctx.holding then
        if ctx.pos60 < P.pos_max and ctx.close > ctx.ma5 and ctx.pct > 0 then
            return "BUY"
        end
    else
        if ctx.gain >= P.tp / 100 then return "SELL" end
        if ctx.gain <= P.sl / 100 then return "SELL" end
    end
    return "HOLD"
end
