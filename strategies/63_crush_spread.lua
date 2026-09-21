-- ============================================================
-- 63_crush_spread.lua -- 压榨价差 (移植 tqsdk demo/example/crush_spread.py)
-- 压榨价差 = 8*豆粕 + 2*豆油 - 10*大豆 ; z-score 均值回复
-- 做多: z<-2 ; 做空: z>2 ; 平仓: |z|<0.5 / 不利偏离*1.5 / 超期(max_days)
-- 三腿名义权重: SPREAD_RATIOS 在引擎内按 {豆-10, 粕+8, 油+2} 应用。
-- ============================================================
strategy = { name = "压榨价差", desc = "3合约压榨价差 z-score 均值回归" }
params = {
    { key="lookback",   label="回溯日",   min=5,  max=60,  step=1,  int=true,  value=30 },
    { key="std_thr",    label="标准差阈值", min=0.5,max=5,   step=0.1,  value=2 },
    { key="close_thr",  label="平仓阈值",  min=0,  max=2,   step=0.05, value=0.5 },
    { key="stop_mult",  label="止损倍数",  min=1,  max=5,   step=0.1,  value=1.5 },
    { key="max_days",   label="最大持仓日", min=1,  max=40,  step=1,  int=true,  value=10 },
    { key="commission", label="佣金%",    min=0,  max=5,  step=0.01, value=0.1 },
    { key="slippage",   label="滑点%",    min=0,  max=5,  step=0.01, value=0.1 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

-- 压榨价差三腿权重: 豆 -10, 粕 +8, 油 +2
SPREAD_RATIOS = { -10, 8, 2 }
local side=0

local function winmeanstd(arr, last, period)
    local s=0
    for j=last-period+1,last do s=s+arr[j] end
    local m=s/period; local sq=0
    for j=last-period+1,last do local d=arr[j]-m; sq=sq+d*d end
    sq=sq/period
    return m, (sq>0) and math.sqrt(sq) or 0
end

function on_bar(ctx)
    local last=ctx.i+1; local sp=ctx.diff_arr
    if last<P.lookback then return "HOLD" end
    local mean, sd = winmeanstd(sp, last, P.lookback)
    local z=(sd>0) and (sp[last]-mean)/sd or 0
    if ctx.holding then
        if math.abs(z)<P.close_thr then side=0; return "HOLD" end
        if side>0 and z>P.stop_mult*P.std_thr then side=0; return "HOLD" end
        if side<0 and z<-P.stop_mult*P.std_thr then side=0; return "HOLD" end
        if ctx.days>=P.max_days then side=0; return "HOLD" end
        return side>0 and "BUY" or "SELL"
    else
        if z<-P.std_thr then side=1; return "BUY" end
        if z>P.std_thr then side=-1; return "SELL" end
        return "HOLD"
    end
end
