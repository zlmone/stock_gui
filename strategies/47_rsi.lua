-- ============================================================
-- 47_rsi.lua -- RSI 超买超卖反转 (移植 tqsdk demo/example/RSI.py)
-- 仅长仓: 超卖回升(prev<超卖 且 cur>超卖) 买入; 止盈(>超买)/止损/时间止损平仓。
-- ============================================================
strategy = { name = "RSI", desc = "RSI 超买超卖反转, 仅长仓做多" }
params = {
    { key="period",     label="RSI周期",  min=2,  max=30,  step=1,  int=true,  value=6 },
    { key="overbought", label="超买阈值", min=50, max=90,  step=1,  value=65 },
    { key="oversold",   label="超卖阈值", min=10, max=50,  step=1,  value=35 },
    { key="stop_loss",  label="止损%",    min=0,  max=30,  step=0.1, value=1 },
    { key="time_stop",  label="时间止损日", min=1, max=60,  step=1,  int=true,  value=10 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function rsi(arr, last, period)
    if last-1 < period then return 50 end   -- 需 period 次变化
    local up=0; local dn=0
    for j=last-period+1,last do local d=arr[j]-arr[j-1]; if d>0 then up=up+d else dn=dn-d end end
    if up+dn==0 then return 50 end
    return 100*up/(up+dn)
end

-- 模块级状态 (跨 on_bar 保持)
local current_direction=0
local entry_price=0
local stop_loss_price=0
local entry_i=0
local was_oversold=false

function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period+10 then return "HOLD" end   -- 足够数据窗口(与原码一致)
    local cl=ctx.close_arr
    local rsi_cur = rsi(cl, last, P.period)
    local rsi_prev = rsi(cl, last-1, P.period)
    if rsi_prev<P.oversold then was_oversold=true end   -- 更新超卖状态
    if current_direction==0 then
        if was_oversold and rsi_prev<P.oversold and rsi_cur>P.oversold then
            current_direction=1
            entry_price=ctx.close
            stop_loss_price=entry_price*(1-P.stop_loss/100)
            entry_i=last
            return "BUY"
        end
        return "HOLD"
    else
        if ctx.close<=stop_loss_price then current_direction=0; return "SELL" end
        if rsi_cur>P.overbought then current_direction=0; return "SELL" end
        if last-entry_i>=P.time_stop then current_direction=0; return "SELL" end
        return "HOLD"
    end
end
