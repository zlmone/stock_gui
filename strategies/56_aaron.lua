-- ============================================================
-- 59_aaron.lua -- Aroon (移植 tqsdk demo/example/Aroon.py, 仅长仓版)
-- 仅长仓: 交叉(cross_up)或强势(cur_up>75 & cur_down<25)买入; cross_down 或 止损平仓。
-- ============================================================
strategy = { name = "Aroon", desc = "Aroon 交叉强势, 仅长仓" }
params = {
    { key="period", label="周期", min=2, max=30, step=1, int=true,  value=10 },
    { key="upper",  label="上阈值", min=50, max=100, step=1,  value=75 },
    { key="lower",  label="下阈值", min=0, max=50, step=1,  value=25 },
    { key="stop_pct", label="止损%", min=0, max=10, step=0.1, value=1.2 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function aroon(hi, lo, last, period)
    if last-1 < period then return 50,50 end
    local pos_h=1; local pos_l=1; local hv=-1e18; local lv=1e18
    for j=last-period+1,last do
        local h=hi[j]; local l=lo[j]
        if h and h>hv then hv=h; pos_h=j end
        if l and l<lv then lv=l; pos_l=j end
    end
    local up=(period-(last-pos_h))/period*100
    local dn=(period-(last-pos_l))/period*100
    return up, dn
end

function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period+1 then return "HOLD" end
    local up_cur, dn_cur = aroon(ctx.high_arr, ctx.low_arr, last, P.period)
    local up_prev, dn_prev = aroon(ctx.high_arr, ctx.low_arr, last-1, P.period)
    local cross_up = up_prev<=dn_prev and up_cur>dn_cur
    local cross_down = up_prev>dn_prev and up_cur<dn_cur
    local strong_up = up_cur>P.upper and dn_cur<P.lower
    if ctx.holding then
        if ctx.close<=ctx.buy_price*(1-P.stop_pct/100) then return "SELL" end
        if cross_down then return "SELL" end
        return "HOLD"
    else
        if cross_up or strong_up then return "BUY" end
        return "HOLD"
    end
end
