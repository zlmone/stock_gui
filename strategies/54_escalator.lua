-- ============================================================
-- 54_escalator.lua -- 自动扶梯 (移植 tqsdk demo/example/escalator.py, 仅长仓版)
-- kline_range=(close-low)/(high-low); 仅长仓: 前根收在MA之上且实体由低跳高 → 买入; 前两根低点-止损点平仓。
-- ============================================================
strategy = { name = "自动扶梯", desc = "K线实体位置+MA, 仅长仓" }
params = {
    { key="slow",    label="慢MA",    min=2,  max=80,  step=1,  int=true,  value=8 },
    { key="fast",    label="快MA",    min=2,  max=60,  step=1,  int=true,  value=40 },
    { key="min_low", label="实体下界", min=0,  max=1,   step=0.01, value=0.25 },
    { key="max_high", label="实体上界", min=0,  max=1,   step=0.01, value=0.75 },
    { key="stop",    label="止损点数", min=1,  max=200, step=1,  value=100 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function ma(arr, period, last)
    if last-1 < period then return 0 end
    local s=0
    for j=last-period+1,last do local v=arr[j]; if v then s=s+v end end
    return s/period
end

function on_bar(ctx)
    local last=ctx.i+1
    local cl=ctx.close_arr; local hi=ctx.high_arr; local lo=ctx.low_arr
    if last<P.fast+2 then return "HOLD" end
    local function klr(i) local d=hi[i]-lo[i]; if d<=0 then return 0 end return (cl[i]-lo[i])/d end
    local ma_slow=ma(cl, P.slow, last); local ma_fast=ma(cl, P.fast, last)
    if ctx.holding then
        local sl=math.min(lo[last-1],lo[last-2])-P.stop
        if ctx.close<=sl then return "SELL" end
        return "HOLD"
    else
        if cl[last-1]>math.max(ma_slow,ma_fast) and klr(last-2)<=P.min_low and klr(last-1)>=P.max_high then return "BUY" end
        return "HOLD"
    end
end
