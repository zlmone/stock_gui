-- W&R 威廉指标: WR=-(最高-收盘)/(最高-最低)*100, 周期20; 正向>90 买入, <10 卖出。
-- 来源: ea-python W&R指标策略.py; talib.WILLR 取负转正。
strategy = { name = "威廉指标 W&R", desc = "正向WR >90 买, <10 卖" }
params = {
    { key="period", label="WR周期", min=2, max=30,  step=1,  int=true,  value=20 },
    { key="over_sell", label="超卖线", min=50, max=100,  step=1,  value=90 },
    { key="over_buy", label="超买线", min=0, max=50,  step=1,  value=10 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_min(a,f,t) local m=math.huge for i=f,t do if a[i]<m then m=a[i] end end return m end
local function arr_max(a,f,t) local m=-math.huge for i=f,t do if a[i]>m then m=a[i] end end return m end
function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period then return "HOLD" end
    local hn=arr_max(ctx.high_arr,last-P.period+1,last)
    local ln=arr_min(ctx.low_arr,last-P.period+1,last)
    if hn<=ln then return "HOLD" end
    local wr = -(ctx.close_arr[last]-ln)/(hn-ln)*100
    if not ctx.holding then
        if wr<P.over_sell then return "BUY" end
        return "HOLD"
    else
        if wr<P.over_buy then return "SELL" end
        return "HOLD"
    end
end
