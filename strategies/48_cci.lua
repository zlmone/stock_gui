-- ============================================================
-- 48_cci.lua -- CCI 均值回归 (移植 tqsdk demo/example/CCI.py, 仅长仓版)
-- CCI=(tp-SMA)/(0.015*MAD); 仅长仓: 曾<-100 现回到中间区 买入; 曾>+100 现回到中间区 平仓。
-- ============================================================
strategy = { name = "CCI", desc = "CCI 均值回归, 仅长仓做多" }
params = {
    { key="period", label="CCI周期", min=2, max=60, step=1, int=true,  value=10 },
    { key="upper",  label="上轨阈值", min=50, max=300, step=1,  value=100 },
    { key="lower",  label="下轨阈值", min=-300, max=-50, step=1, value=-100 },
    { key="stop_loss", label="止损%", min=0, max=30, step=0.1, value=1 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function cci(hi, lo, cl, last, period)
    if last-1 < period then return 0 end
    local tp=0; local tpsum=0; local mad=0
    for j=last-period+1,last do local v=(hi[j]+lo[j]+cl[j])/3; tpsum=tpsum+v end
    local tpavg=tpsum/period
    for j=last-period+1,last do local v=(hi[j]+lo[j]+cl[j])/3; mad=mad+math.abs(v-tpavg) end
    mad=mad/period
    if mad==0 then return 0 end
    return ((hi[last]+lo[last]+cl[last])/3 - tpavg) / (0.015*mad)
end

local last_state=0

function on_bar(ctx)
    local last=ctx.i+1
    if last<P.period+10 then return "HOLD" end
    local cur=cci(ctx.high_arr,ctx.low_arr,ctx.close_arr,last,P.period)
    local prev=cci(ctx.high_arr,ctx.low_arr,ctx.close_arr,last-1,P.period)
    local st_cur = cur>P.upper and 1 or (cur<P.lower and -1 or 0)
    local st_prev = last_state
    if ctx.holding then
        if st_prev==1 and st_cur==0 then last_state=st_cur; return "SELL" end
        if ctx.close <= ctx.buy_price*(1-P.stop_loss/100) then last_state=st_cur; return "SELL" end
        last_state=st_cur; return "HOLD"
    else
        if st_prev==-1 and st_cur==0 then last_state=st_cur; return "BUY" end
        last_state=st_cur; return "HOLD"
    end
end
