-- ============================================================
-- 44_momentum.lua -- 价格动量 AR (移植 tqsdk demo/example/momentum.py)
-- ar = (Σ(high-open) / Σ(open-low)) ×100, 取最近 N-1 根(不含当前bar)。
-- 原: 110<ar<150 做多(仅长:BUY); ar<90 止损平仓(SELL)。
-- ============================================================
strategy = { name = "价格动量AR", desc = "AR 动量区间, 仅长仓做多" }
params = {
    { key="N",       label="动量天数", min=5,  max=60,  step=1,  int=true,  value=15 },
    { key="buy_hi",  label="做多上界", min=100, max=300, step=1,  value=150 },
    { key="buy_lo",  label="做多下界", min=0,  max=300, step=1,  value=110 },
    { key="sell_th", label="平仓止损线", min=0, max=300, step=1,  value=90 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    local last=ctx.i+1
    if last<P.N then return "HOLD" end   -- 窗口: 1-index last-N+1 .. last-1
    local hh=0; local oc=0
    for j=last-P.N+1,last-1 do
        hh=hh+(ctx.high_arr[j]-ctx.open_arr[j])
        oc=oc+(ctx.open_arr[j]-ctx.low_arr[j])
    end
    if oc<=0 then oc=ctx.low_arr[last]*0.001 end   -- 原用 price_tick 防除零
    local ar=hh/oc*100
    if ctx.holding then
        if ar<P.sell_th then return "SELL" end
        return "HOLD"
    else
        if ar>P.buy_lo and ar<P.buy_hi then return "BUY" end
        return "HOLD"
    end
end
