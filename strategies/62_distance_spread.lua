-- ============================================================
-- 62_distance_spread.lua -- 距离/标准化价差 (移植 tqsdk demo/example/distance_based.py)
-- 规范化: norm1=(A-meanA)/stdA, norm2=(B-meanB)/stdB; 价差 S=norm1-norm2
-- 买入: S 低于 meanS-K*stdS ; 卖出: S 高于 meanS+K*stdS
-- 平仓: 均值回归(|S-meanS|<close_thr*stdS)/时间(max_days)/A-B 结算亏损(stop_pct)。
-- ============================================================
strategy = { name = "距离价差", desc = "规范化(z-score)价差均值回归" }
params = {
    { key="window",   label="窗口",    min=5,  max=60,  step=1,  int=true,  value=30 },
    { key="k_thr",    label="K阈值",   min=0.5,max=5,   step=0.1,  value=2 },
    { key="close_thr", label="平仓阈值", min=0,  max=2,   step=0.05, value=0.5 },
    { key="max_days",  label="最大持仓日", min=1,  max=40,  step=1,  int=true,  value=10 },
    { key="stop_pct",  label="止损%",   min=1,  max=20,  step=0.5,  value=5 },
    { key="commission", label="佣金%",   min=0,  max=5,  step=0.01, value=0.1 },
    { key="slippage",  label="滑点%",   min=0,  max=5,  step=0.01, value=0.1 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local side=0

local function winmeanstd(arr, last, period)
    local s=0
    for j=last-period+1,last do s=s+arr[j] end
    local m=s/period
    local sq=0
    for j=last-period+1,last do local d=arr[j]-m; sq=sq+d*d end
    sq=sq/period
    return m, (sq>0) and math.sqrt(sq) or 0
end

function on_bar(ctx)
    local last=ctx.i+1; local a=ctx.a_arr; local b=ctx.b_arr
    if last<P.window then return "HOLD" end
    local ma, sa = winmeanstd(a, last, P.window)
    local mb, sb = winmeanstd(b, last, P.window)
    local ssum=0; local ssum2=0
    for j=last-P.window+1,last do
        local n1=(sa>0) and (a[j]-ma)/sa or 0
        local n2=(sb>0) and (b[j]-mb)/sb or 0
        local sp=n1-n2; ssum=ssum+sp; ssum2=ssum2+sp*sp
    end
    local meanS=ssum/P.window
    local varS=ssum2/P.window-meanS*meanS
    local stdS=(varS>0) and math.sqrt(varS) or 0
    local n1c=(sa>0) and (a[last]-ma)/sa or 0
    local n2c=(sb>0) and (b[last]-mb)/sb or 0
    local cur=n1c-n2c
    if ctx.holding then
        if math.abs(cur-meanS)<P.close_thr*stdS then side=0; return "HOLD" end
        if ctx.days>=P.max_days then side=0; return "HOLD" end
        if ctx.gain<=-P.stop_pct then side=0; return "HOLD" end
        return side>0 and "BUY" or "SELL"
    else
        if cur<meanS-P.k_thr*stdS then side=1; return "BUY" end
        if cur>meanS+P.k_thr*stdS then side=-1; return "SELL" end
        return "HOLD"
    end
end
