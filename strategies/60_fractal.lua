-- ============================================================
-- 60_fractal.lua -- 分形趋势突破 (移植 tqsdk demo/example/fractal-trend.py, 仅长仓版)
-- 牛市分形:中间bar低点低于两侧;价突破分形高点+短MA>长MA 买入。
-- 平仓:跌破止损(分形低点-1.2ATR)/止盈/跌破短MA/新熊市分形跌破。
-- ============================================================
strategy = { name = "分形", desc = "分形突破+趋势确认, 仅长仓" }
params = {
    { key="fw",    label="分形窗口", min=1, max=3, step=1, int=true,  value=2 },
    { key="short", label="短MA",    min=2, max=30, step=1, int=true,  value=6 },
    { key="long",  label="长MA",    min=3, max=40, step=1, int=true,  value=12 },
    { key="atr_period", label="ATR周期", min=5, max=40, step=1, int=true,  value=10 },
    { key="atr_mult", label="ATR止损倍", min=0.2, max=3, step=0.1, value=1.2 },
    { key="tp_ratio", label="止盈倍数", min=0.5, max=5, step=0.1, value=1.8 },
    { key="recent", label="分形近期日", min=1, max=20, step=1, int=true,  value=10 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function ma(arr, period, last)
    if last-1 < period then return 0 end
    local s=0
    for j=last-period+1,last do local v=arr[j]; if v then s=s+v end end
    return s/period
end

local function atr(hi, lo, cl, last, period)
    if last < period+1 then return 0 end
    local s=0
    for j=last-period+1,last do local pc=cl[j-1]
        s=s+math.max(hi[j]-lo[j], math.abs(hi[j]-pc), math.abs(lo[j]-pc)) end
    return s/period
end

-- 返回最近确认的牛分形(含低点)或nil。分形中心i需 i+fw<=last 才确认。
local function last_bull(hi, lo, last)
    local fw=P.fw
    for i=last-2*fw, 3, -1 do
        local ok=true
        for j=1,fw do if lo[i]>=lo[i-j] or lo[i]>=lo[i+j] then ok=false; break end end
        if ok then return i, lo[i] end
    end
    return nil
end

local function last_bear(hi, lo, last)
    local fw=P.fw
    for i=last-2*fw, 3, -1 do
        local ok=true
        for j=1,fw do if hi[i]<=hi[i-j] or hi[i]<=hi[i+j] then ok=false; break end end
        if ok then return i, hi[i] end
    end
    return nil
end

function on_bar(ctx)
    local last=ctx.i+1
    local cl=ctx.close_arr; local hi=ctx.high_arr; local lo=ctx.low_arr
    if last<P.long+P.fw*2+5 then return "HOLD" end
    local uptrend=ma(cl, P.short, last) > ma(cl, P.long, last)
    if ctx.holding then
        if ctx.close<=ctx.stop_loss then return "SELL" end
        if ctx.close>=ctx.take_profit then return "SELL" end
        if ctx.close<ma(cl, P.short, last) then return "SELL" end
        local bi, bh = last_bear(hi, lo, last)
        if bi and bi > last-P.recent and ctx.close<bh then return "SELL" end
        return "HOLD"
    else
        local bi, bl, bh = nil, nil, nil
        local fw=P.fw
        for i=last-2*fw, 3, -1 do
            local ok=true
            for j=1,fw do if lo[i]>=lo[i-j] or lo[i]>=lo[i+j] then ok=false; break end end
            if ok then bi=i; bl=lo[i]; bh=hi[i]; break end
        end
        if bi and ctx.close>bh and uptrend then
            local n=atr(hi,lo,cl,last,P.atr_period)
            ctx.stop_loss = bl - P.atr_mult*n
            ctx.take_profit = ctx.close + (ctx.close - ctx.stop_loss)*P.tp_ratio
            return "BUY"
        end
        return "HOLD"
    end
end
