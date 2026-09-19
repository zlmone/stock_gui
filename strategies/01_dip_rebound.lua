-- 急杀反弹: 大阴线急杀后买入, 止盈 / 到期平仓
strategy = { name = "急杀反弹", desc = "单日跌幅达阈值买入, 止盈或到期平仓" }

-- 可调参数 (界面自动渲染, 运行时注入 P 表)
params = {
    { key="dip",  label="急杀阈值%",  min=-15, max=-3, step=0.5, value=-7 },
    { key="tp",   label="止盈%",     min=2,   max=20, step=0.5, value=5 },
    { key="hold", label="最长持有日", min=1,   max=20, step=1, int=true, value=5 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    if not ctx.holding then
        if ctx.pct <= P.dip then
            return "BUY"
        end
    else
        if ctx.gain >= P.tp / 100 then return "SELL" end
        if ctx.days >= P.hold then return "SELL" end
    end
    return "HOLD"
end
