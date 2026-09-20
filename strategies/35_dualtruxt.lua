-- Dual Thrust 通道突破: 突破上轨买入, 突破下轨卖出, 跌破止损线强平。
-- 来源: ea-python Dual Thrust策略.py; 区间取前 window 根(不含今日), 上轨=今开+K1*区间, 下轨=今开-K2*区间。
strategy = { name = "Dual Thrust 突破", desc = "突破前日区间轨道买/卖, 跌破75%止损" }
params = {
    { key="window",  label="区间天数", min=2, max=20,  step=1,  int=true,  value=5 },
    { key="k1",      label="上轨系数", min=0, max=2,  step=0.05,  value=0.2 },
    { key="k2",      label="下轨系数", min=0, max=2,  step=0.05,  value=0.5 },
    { key="stop_loss", label="止损线(净值×初始)", min=0, max=1,  step=0.05,  value=0.75 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_min(a,f,t) local m=math.huge for i=f,t do if a[i]<m then m=a[i] end end return m end
local function arr_max(a,f,t) local m=-math.huge for i=f,t do if a[i]>m then m=a[i] end end return m end
local initial_net  -- 初始净值(文件级, 用于止损基准)
local stopped      -- 止损已触发(文件级)
function on_bar(ctx)
    local last = ctx.i + 1
    if initial_net == nil then initial_net = ctx.equity end
    if stopped then return "HOLD" end
    if ctx.equity < P.stop_loss * initial_net then stopped = true; if ctx.holding then return "SELL" end; return "HOLD" end
    if last < P.window + 1 then return "HOLD" end
    local hh = arr_max(ctx.high_arr, last-P.window, last-1)
    local hc = arr_max(ctx.close_arr, last-P.window, last-1)
    local lc = arr_min(ctx.close_arr, last-P.window, last-1)
    local ll = arr_min(ctx.low_arr, last-P.window, last-1)
    local range = math.max(hh-lc, hc-ll)
    local up   = ctx.open_arr[last] + P.k1 * range
    local low  = ctx.open_arr[last] - P.k2 * range
    if not ctx.holding then
        if ctx.close_arr[last] > up then return "BUY" end
        return "HOLD"
    else
        if ctx.close_arr[last] < low then return "SELL" end
        return "HOLD"
    end
end
