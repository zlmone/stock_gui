-- ============================================================
-- 50_trix.lua -- TRIX 三重EMA差量穿越 (移植 tqsdk demo/example/TRIX.py, 仅长仓版)
-- 仅长仓: TRIX上穿信号线做多; 止损(2N ATR)/止盈(3N ATR)/下穿信号平仓。
-- ============================================================
strategy = { name = "TRIX", desc = "三重EMA差量穿越, 仅长仓" }
params = {
    { key="period",     label="TRIX周期", min=2,  max=40,  step=1,  int=true,  value=12 },
    { key="signal",     label="信号周期", min=2,  max=30,  step=1,  int=true,  value=9 },
    { key="atr_period", label="ATR周期",  min=5,  max=40,  step=1,  int=true,  value=14 },
    { key="stop_mult",  label="止损ATR倍",min=0.5,max=5,   step=0.1, value=2 },
    { key="tp_mult",    label="止盈ATR倍",min=0.5,max=5,   step=0.1, value=3 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function ema(arr, period, upto)
    local alpha = 2/(period+1); local out={}; local prev=nil
    for i=1,upto do local v=arr[i]; if v then if prev==nil then prev=v else prev=alpha*v+(1-alpha)*prev end out[i]=prev end end
    return out
end

local function atr(hi, lo, cl, last, period)
    if last < period+1 then return 0 end
    local s=0
    for j=last-period+1,last do local pc=cl[j-1]
        local tr=math.max(hi[j]-lo[j], math.abs(hi[j]-pc), math.abs(lo[j]-pc)); s=s+tr end
    return s/period
end

local function signal_ma(trix, period, last)
    if last-1 < period then return 0 end
    local s=0
    for j=last-period+1,last do local v=trix[j]; if v then s=s+v end end
    return s/period
end

function on_bar(ctx)
    local last=ctx.i+1
    local cl=ctx.close_arr; local hi=ctx.high_arr; local lo=ctx.low_arr
    if last<P.period+10 then return "HOLD" end
    local e1=ema(cl, P.period, last); local e2=ema(e1, P.period, last); local e3=ema(e2, P.period, last)
    local trix={}
    for j=1,last do if e3[j] and e3[j-1] and e3[j-1]~=0 then trix[j]=100*(e3[j]/e3[j-1]-1) end end
    local trix_cur=trix[last]; local trix_prev=trix[last-1]
    local sig_cur=signal_ma(trix, P.signal, last); local sig_prev=signal_ma(trix, P.signal, last-1)
    if ctx.holding then
        local n=atr(hi,lo,cl,last,P.atr_period); local bp=ctx.buy_price
        if ctx.close<=bp-P.stop_mult*n then return "SELL" end
        if ctx.close>=bp+P.tp_mult*n then return "SELL" end
        if sig_prev>sig_cur then return "SELL" end
        return "HOLD"
    else
        if sig_prev<sig_cur and trix_cur>sig_cur then return "BUY" end
        return "HOLD"
    end
end
