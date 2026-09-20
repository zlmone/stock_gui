-- AROON 阿隆: 上升/下降线方向配合 50/70 触发买卖。
-- 来源: ea-python 阿隆指标策略.py; talib.AROON(period=20)。
strategy = { name = "AROON 阿隆", desc = "上升线优势过50/70买, 下降线优势过50/70卖" }
params = { { key="period", label="AROON周期", min=2, max=30,  step=1,  int=true,  value=20 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_min(a,f,t) local m=math.huge for i=f,t do if a[i]<m then m=a[i] end end return m end
local function arr_max(a,f,t) local m=-math.huge for i=f,t do if a[i]>m then m=a[i] end end return m end
local function calcAROON(high, low, close, period)
    local n = #close
    local up = {}; local down = {}
    for i=1,n do up[i]=0; down[i]=0 end
    for i=period,n do
        local hi=high[i]; local hiIdx=i; local lo=low[i]; local loIdx=i
        for j=i-period+1,i do
            if high[j]>hi then hi=high[j]; hiIdx=j end
            if low[j]<lo then lo=low[j]; loIdx=j end
        end
        up[i]=(period-(i-hiIdx))/period*100
        down[i]=(period-(i-loIdx))/period*100
    end
    return up, down
end
function on_bar(ctx)
    local last = ctx.i + 1
    if last<P.period then return "HOLD" end
    local up, down = calcAROON(ctx.high_arr, ctx.low_arr, ctx.close_arr, P.period)
    local u = up[last]; local d = down[last]
    local up2 = (last>1) and up[last-1] or 0; local dn2 = (last>1) and down[last-1] or 0
    if not ctx.holding then
        if u > d then
            if (d<50 and dn2>=50) or (u>70 and up2<=70) then return "BUY" end
        end
        return "HOLD"
    else
        if u < d then
            if (u<50 and up2>=50) or (d>70 and dn2<=70) then return "SELL" end
        end
        return "HOLD"
    end
end
