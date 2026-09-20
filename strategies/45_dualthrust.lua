-- ============================================================
-- 45_dualthrust.lua -- 双轨 Dual Thrust (移植 tqsdk demo/example/dualthrust.py)
-- 用近 NDAY 根高低算 range, 当前开股价 ±range·K 定上下轨; 仅长仓: 收>上轨做多, 跌破下轨平仓。
-- ============================================================
strategy = { name = "双轨", desc = "Dual Thrust 通道突破, 仅长仓做多" }
params = {
    { key="NDAY", label="轨道天数", min=1, max=30, step=1, int=true,  value=5 },
    { key="K1",   label="上轨K值",  min=0, max=1,  step=0.01, value=0.2 },
    { key="K2",   label="下轨K值",  min=0, max=1,  step=0.01, value=0.2 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    local last=ctx.i+1
    if last<P.NDAY+1 then return "HOLD" end   -- 窗口 1-index last-NDAY .. last-1
    local h=ctx.high_arr; local lo=ctx.low_arr; local cl=ctx.close_arr; local op=ctx.open_arr
    local HH=-1e18; local HC=-1e18; local LC=1e18; local LL=1e18
    for j=last-P.NDAY,last-1 do
        if h[j]>HH then HH=h[j] end
        if cl[j]>HC then HC=cl[j] end
        if cl[j]<LC then LC=cl[j] end
        if lo[j]<LL then LL=lo[j] end
    end
    local rng = (HH-LC) > (HC-LL) and (HH-LC) or (HC-LL)
    local buy_line   = op[last] + rng*P.K1
    local sell_line  = op[last] - rng*P.K2
    if ctx.holding then
        if ctx.close < sell_line then return "SELL" end
        return "HOLD"
    else
        if ctx.close > buy_line then return "BUY" end
        return "HOLD"
    end
end
