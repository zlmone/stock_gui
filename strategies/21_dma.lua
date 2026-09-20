-- DMA 平行线差: 差值(dma)上穿均值(ama)时买入, 下穿时卖出。
-- 来源: ea-python DMA指标策略.py; SMA5-SMA20  vs  SMA(dma,10)。
strategy = { name = "DMA 平行线差", desc = "dma 上穿 ama 买, 下穿卖" }
params = {
    { key="short_window",  label="短周期", min=1, max=30,  step=1,  int=true,  value=5 },
    { key="long_window",   label="长周期", min=5, max=60,  step=1,  int=true,  value=20 },
    { key="ama_window",    label="差值均线", min=2, max=30,  step=1,  int=true,  value=10 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_mean(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s/(t-f+1) end
local function calcDMA(close, short, long, ama_window)
    local n = #close; local smaS={}; local smaL={}; local dma={}; local ama={}
    for i=1,n do
        if i>=short then smaS[i]=arr_mean(close,i-short+1,i) else smaS[i]=0 end
        if i>=long  then smaL[i]=arr_mean(close,i-long+1,i)  else smaL[i]=0 end
    end
    for i=1,n do dma[i]=smaS[i]-smaL[i] end
    for i=1,n do ama[i]=(i>=ama_window) and arr_mean(dma,i-ama_window+1,i) or 0 end
    return smaS, smaL, dma, ama
end
function on_bar(ctx)
    local last = ctx.i + 1
    local _, _, dma, ama = calcDMA(ctx.close_arr, P.short_window, P.long_window, P.ama_window)
    if last < P.long_window then return "HOLD" end
    if not ctx.holding then
        if dma[last] > ama[last] then return "BUY" end
        return "HOLD"
    else
        if dma[last] < ama[last] then return "SELL" end
        return "HOLD"
    end
end
