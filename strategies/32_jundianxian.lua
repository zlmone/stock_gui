-- 简单均线突破: 收盘 > 近5根收盘均价买入, 均价下方卖出。
-- 来源: ea-python 简单均线突破策略.py (无参数的价格突破策略)。
strategy = { name = "简单均线突破", desc = "收盘突破近5日均价买, 跌破均价卖" }
params = { { key="window", label="均线周期", min=2, max=20,  step=1,  int=true,  value=5 } }
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_mean(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s/(t-f+1) end
function on_bar(ctx)
    local last=ctx.i+1
    if last<P.window then return "HOLD" end
    local ma = arr_mean(ctx.close_arr, last-P.window+1, last)
    if not ctx.holding then
        if ctx.close_arr[last]>ma then return "BUY" end
        return "HOLD"
    else
        if ctx.close_arr[last]<ma then return "SELL" end
        return "HOLD"
    end
end
