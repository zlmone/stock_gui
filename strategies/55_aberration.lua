-- ============================================================
-- 55_aberration.lua -- Aberration 布林带 (移植 tqsdk demo/example/aberration.py, 仅长仓版)
-- 仅长仓: 收上轨做多; 跌破中轨平仓。
-- ============================================================
strategy = { name = "Aberration", desc = "布林带突破中轨法, 仅长仓" }
params = {
    { key="period", label="布林周期", min=5, max=60, step=1, int=true,  value=26 },
    { key="p",      label="标准差倍数", min=1, max=5, step=0.1, value=2 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    local last=ctx.i+1
    local cl=ctx.close_arr
    if last<P.period+1 then return "HOLD" end
    local sum=0
    for j=last-P.period+1,last do local v=cl[j]; if v then sum=sum+v end end
    local mid=sum/P.period
    local vsum=0
    for j=last-P.period+1,last do local d=cl[j]-mid; vsum=vsum+d*d end
    local std=math.sqrt(vsum/P.period)
    local upper=mid+P.p*std; local lower=mid-P.p*std
    if ctx.holding then
        if ctx.close<mid then return "SELL" end
        return "HOLD"
    else
        if ctx.close>upper then return "BUY" end
        return "HOLD"
    end
end
