-- ============================================================
-- 43_doublema.lua -- 双均线 (移植 tqsdk demo/example/doublema.py)
-- 原策略: 短均线上穿长均线 -> set_target_volume(3) 做多;
--        长均线下穿(短穿长) -> set_target_volume(-3) 做空。
-- 本引擎仅做多: 金叉 -> BUY, 死叉 -> SELL 平仓。
-- 注: 本 Lua 构建单语句不支持逗号分隔的多个 locals;
--     表字段必须用逗号分隔, 多 locals 用分号分开。
-- ============================================================
strategy = { name = "双均线", desc = "short/long SMA 金叉做多/死叉平仓" }
params = {
    { key="short",    label="短周期",    min=2,  max=120,  step=1,  int=true,  value=30 },
    { key="long",     label="长周期",    min=10, max=300,  step=1,  int=true,  value=60 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function ma(arr, period, last)
    local s=0
    for j=last-period+1,last do local v=arr[j]; if v then s=s+v end end
    return s/period
end

function on_bar(ctx)
    local last=ctx.i+1   -- 当前bar的1索引
    if last<P.long then return "HOLD" end   -- 数据不足长均线窗口
    local cl=ctx.close_arr
    local s=ma(cl, P.short, last)
    local l=ma(cl, P.long, last)
    local sp=ma(cl, P.short, last-1)
    local lp=ma(cl, P.long, last-1)
    if ctx.holding then
        if sp<=lp and s>l then return "SELL" end   -- 死叉平仓
        return "HOLD"
    else
        if sp<=lp and s>l then return "BUY" end     -- 金叉做多
        return "HOLD"
    end
end
