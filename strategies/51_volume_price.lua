-- ============================================================
-- 51_volume_price.lua -- VPT 量价趋势 (移植 tqsdk demo/example/volume_price.py, 仅长仓版)
-- VPT=Σvolume×(价格变化%); 仅长仓: VPT>均线且放量+收涨 买入, VPT<均线且放量 平仓。
-- ============================================================
strategy = { name = "量价VPT", desc = "VPT 量价趋势, 仅长仓" }
params = {
    { key="ma_period", label="VPT均线周期", min=2, max=40, step=1, int=true,  value=14 },
    { key="vol_mult",  label="放量倍数",    min=1, max=5,   step=0.1,  value=1.5 },
}

P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    local last=ctx.i+1
    local cl=ctx.close_arr; local vol=ctx.vol_arr
    if last<P.ma_period+1 then return "HOLD" end
    local vpt={}
    for i=1,last do
        if i==1 then vpt[i]=0
        else vpt[i]=vpt[i-1] + vol[i]*((cl[i]-cl[i-1])/cl[i-1]) end
    end
    local vpt_cur=vpt[last]; local vpt_ma=0
    for j=last-P.ma_period+1,last do local v=vpt[j]; if v then vpt_ma=vpt_ma+v end end
    vpt_ma=vpt_ma/P.ma_period
    local vol_ma=0; for j=last-P.ma_period,last-1 do local v=vol[j]; if v then vol_ma=vol_ma+v end end
    vol_ma=vol_ma/(P.ma_period-1)
    local vol_incr = vol[last] > vol_ma*P.vol_mult
    if ctx.holding then
        if vpt_cur<vpt_ma and vol_incr then return "SELL" end
        return "HOLD"
    else
        if vpt_cur>vpt_ma and vol_incr and cl[last]>cl[last-1] then return "BUY" end
        return "HOLD"
    end
end
