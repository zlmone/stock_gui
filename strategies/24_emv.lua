-- EMV 简易运动: EMV 向上穿越0轴买入, 向下穿越0轴卖出。
-- 来源: ea-python EMV指标策略.py; EMV=周期内 (a-b)*c/vol 滚动和。
strategy = { name = "EMV 简易运动", desc = "EMV 上穿0轴买, 下穿0轴卖" }
params = { { key="period", label="EMV周期", min=1, max=30,  step=1,  int=true,  value=6 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function calcEMV(high, low, vol, period)
    local n = #high
    local em  = {}; local emv = {}
    for i=1,n do emv[i]=0 end
    for i=1,n do
        if i==1 then em[i]=0
        else
            local a1=(high[i]+low[i])/2; local a0=(high[i-1]+low[i-1])/2
            local c=high[i]-low[i]; local v=vol[i]
            em[i]=(v>0) and ((a1-a0)*c/v) or 0
        end
    end
    for i=1,n do
        if i<period then emv[i]=0
        else local s=0 for j=i-period+1,i do s=s+em[j] end emv[i]=s end
    end
    return emv
end
function on_bar(ctx)
    local last = ctx.i + 1
    if last < P.period then return "HOLD" end
    local emv = calcEMV(ctx.high_arr, ctx.low_arr, ctx.vol_arr, P.period)
    local cur = emv[last]; local prev = (last>1) and emv[last-1] or 0
    if not ctx.holding then
        if prev <= 0 and cur > 0 then return "BUY" end
        return "HOLD"
    else
        if prev >= 0 and cur < 0 then return "SELL" end
        return "HOLD"
    end
end
