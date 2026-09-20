-- ============================================================
-- 53_z_score.lua -- Z-Score 均值回归 (移植 tqsdk demo/example/z_score.py, 仅长仓版)
-- 仅长仓: z<-1.8 买入; |z|<=0.4 或 z<-2.5 平仓(外加 max_days 时间止损)。
-- ============================================================
strategy = { name = "Z-Score", desc = "Z-Score 均值回归, 仅长仓" }
params = {
    { key="windowsize", label="Z-Score窗口", min=2, max=60, step=1, int=true,  value=20 },
    { key="entry_z",    label="开仓Z阈值",   min=-3, max=-1, step=0.1,  value=-1.8 },
    { key="max_days",   label="最大持仓日",  min=1, max=60,  step=1,  int=true,  value=8 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    local last=ctx.i+1
    local cl=ctx.close_arr
    if last<P.windowsize+1 then return "HOLD" end
    local sum=0
    for j=last-P.windowsize+1,last do local v=cl[j]; if v then sum=sum+v end end
    local mean=sum/P.windowsize
    local vs=0
    for j=last-P.windowsize+1,last do local d=cl[j]-mean; vs=vs+d*d end
    local std=math.sqrt(vs/P.windowsize)
    if std==0 then return "HOLD" end
    local z=(cl[last]-mean)/std
    if ctx.holding then
        if math.abs(z)<=0.4 or z<=-2.5 then return "SELL" end
        if ctx.days>=P.max_days then return "SELL" end
        return "HOLD"
    else
        if z<P.entry_z then return "BUY" end
        return "HOLD"
    end
end
