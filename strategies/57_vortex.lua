-- ============================================================
-- 60_vortex.lua -- 涡旋VI (移植 tqsdk demo/example/vortex-indicator.py, 仅长仓版)
-- 仅长仓: +VI上穿-VI且>阈值 买入; -VI上穿或ATR止损平仓。
-- ============================================================
strategy = { name = "涡旋VI", desc = "Vortex +VI/-VI, 仅长仓" }
params = {
    { key="vi_period", label="VI周期", min=5, max=40, step=1, int=true,  value=14 },
    { key="atr_period", label="ATR周期", min=5, max=40, step=1, int=true,  value=14 },
    { key="thr",  label="VI阈值", min=0, max=5, step=0.1,  value=1 },
    { key="atr_mult", label="ATR止损倍", min=0.5, max=5, step=0.1, value=2 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function atr(hi, lo, cl, last, period)
    if last < period+1 then return 0 end
    local s=0
    for j=last-period+1,last do local pc=cl[j-1]
        s=s+math.max(hi[j]-lo[j], math.abs(hi[j]-pc), math.abs(lo[j]-pc)) end
    return s/period
end

local function vi(hi, lo, cl, last, period)
    if last < period+1 then return 0,0 end
    local tv=0; local pv=0; local mv=0
    for j=last-period+1,last do local pc=cl[j-1]
        tv=tv+math.max(hi[j]-lo[j], math.abs(hi[j]-pc), math.abs(lo[j]-pc))
        pv=pv+math.abs(hi[j]-cl[j-1])
        mv=mv+math.abs(lo[j]-hi[j-1])
    end
    if tv==0 then return 0,0 end
    return pv/tv, mv/tv
end

function on_bar(ctx)
    local last=ctx.i+1
    local hi=ctx.high_arr; local lo=ctx.low_arr; local cl=ctx.close_arr
    if last<P.vi_period+3 then return "HOLD" end
    local pv_cur, mv_cur = vi(hi, lo, cl, last, P.vi_period)
    local pv_prev, mv_prev = vi(hi, lo, cl, last-1, P.vi_period)
    if ctx.holding then
        local sl=ctx.buy_price-P.atr_mult*atr(hi,lo,cl,last,P.atr_period)
        if ctx.close<=sl then return "SELL" end
        if mv_prev<=pv_prev and mv_cur>pv_cur then return "SELL" end
        return "HOLD"
    else
        if pv_prev<=mv_prev and pv_cur>mv_cur and pv_cur>P.thr then return "BUY" end
        return "HOLD"
    end
end
