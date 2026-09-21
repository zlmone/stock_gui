-- ============================================================
-- bt_harness_spread3.lua -- 三合约(如压榨/套利)价差均值回归回测引擎
-- 用法: ./runlua bt_harness_spread3.lua <策略.lua> [A.csv] [B.csv] [C.csv]
-- 策略须定义 SPREAD_RATIOS = {r1,r2,r3}; 价差 S = r1*A + r2*B + r3*C
-- 策略 on_bar 返回 "BUY"(做涨价差)/"SELL"(做跌价差)/"HOLD"(空仓)
-- 成交收益 = 方向*(价差变化)/建仓时总名义(Σ|rk*pricek|); 开平单边扣 commission+slippage(%)
-- ============================================================
local strat = arg[1]
if not strat then print("用法: runlua bt_harness_spread3.lua <策略.lua> [A] [B] [C]"); return end
local f1 = arg[2] or "tools/data/sym_a.csv"
local f2 = arg[3] or "tools/data/sym_m.csv"
local f3 = arg[4] or "tools/data/sym_y.csv"
local function loadcsv(path)
    local hi={}; local lo={}; local cl={}; local vol={}; local od={}
    for line in io.open(path,"r"):lines() do
        local f={}
        for x in line:gmatch("[^,]+") do f[#f+1]=x end
        if #f>=6 then
            od[#od+1]=f[1]
            cl[#cl+1]=tonumber(f[5]); hi[#hi+1]=tonumber(f[3]); lo[#lo+1]=tonumber(f[4]); vol[#vol+1]=tonumber(f[6])
        end
    end
    return od,cl,hi,lo,vol
end
local od1,c1,h1,l1,vol1 = loadcsv(f1)
local _,c2,h2,l2,vol2 = loadcsv(f2)
local _,c3,h3,l3,vol3 = loadcsv(f3)
local n=math.min(#c1,#c2,#c3)
if n<3 then print("数据不足:", n); return end
local maxBars=(arg[5] and tonumber(arg[5]) or 1e9)
if maxBars>0 and n>maxBars then
    local s=n-maxBars
    local nc1={}; local nc2={}; local nc3={}; local nh1={}; local nl1={}; local nov={}; local nod={}
    for I=s+1,n do nc1[#nc1+1]=c1[I]; nc2[#nc2+1]=c2[I]; nc3[#nc3+1]=c3[I]; nh1[#nh1+1]=h1[I]; nl1[#nl1+1]=l1[I]; nov[#nov+1]=vol1[I]; nod[#nod+1]=od1[I] end
    c1,c2,c3,h1,l1,vol1,od1=nc1,nc2,nc3,nh1,nl1,nov,nod
    n=n-maxBars
end
dofile(strat)
local P={}
if params then for _,v in ipairs(params) do P[v.key]=v.value end end
P.commission=P.commission or 0.1
P.slippage=P.slippage or 0.1
if arg then for a=6,#arg do local k,v=arg[a]:match("^(.+)=(.+)$") if k then P[k]=tonumber(v) or v end end end
local r = SPREAD_RATIOS or {P.r1 or 1, P.r2 or -1, P.r3 or 0}
local spread={}
for i=1,n do spread[i]=r[1]*c1[i]+r[2]*c2[i]+r[3]*c3[i] end
local side=0; local buy_i=0; local buy_date=""; local buy_sp=0; local buy_not=1
local equity=1.0; local gross_equity=1.0; local worst=0
local sum_gain=0; local sum_days=0; local wins=0; local trades={}
local decided={}
for I=2,n do
    local ctx={ i=I-1, date=od1[I], day=I, close=c1[I], close2=c2[I], close3=c3[I],
        holding=(side~=0), buy_price=buy_sp,
        gain=(side~=0) and side*(spread[I]-buy_sp)/math.max(buy_not,1)*100 or 0,
        days=(side~=0) and (I-buy_i) or 0,
        c1_arr=c1, c2_arr=c2, c3_arr=c3,
        diff_arr=spread, ratio_arr=nil,
        a_hi=h1, a_lo=l1, b_hi=h2, b_lo=l2, vol_arr=vol1,
        i1=c1[I], i2=c2[I], i3=c3[I] }
    local sig=on_bar(ctx)
    local desired=(sig=="BUY") and 1 or (sig=="SELL") and -1 or 0
    if desired~=side then
        if side~=0 then
            local se=spread[I]
            local rawpct=side*(se-buy_sp)/math.max(buy_not,1)*100
            local netpct=rawpct-(P.commission+P.slippage)
            trades[#trades+1]={date=od1[buy_i], sell_date=od1[I], side=side, buy_price=buy_sp, buy_A=buy_sp, sell_price=se, days=I-buy_i, gain_pct=netpct, reason=(sig=="HOLD" and "HOLD" or "切换")}
            equity=equity*(1+netpct/100); gross_equity=gross_equity*(1+rawpct/100)
            worst=(worst==0 or netpct<worst) and netpct or worst
            sum_gain=sum_gain+netpct; sum_days=sum_days+(I-buy_i)
            if netpct>0 then wins=wins+1 end
        end
        if desired~=0 then buy_i=I; buy_date=od1[I]; buy_sp=spread[I]; buy_not=math.abs(r[1]*c1[buy_i])+math.abs(r[2]*c2[buy_i])+math.abs(r[3]*c3[buy_i]) end
        side=desired
    end
    decided[I]=side
end
if side~=0 then
    local se=spread[n]
    local rawpct=side*(se-buy_sp)/math.max(buy_not,1)*100
    local netpct=rawpct-(P.commission+P.slippage)
    trades[#trades+1]={date=od1[buy_i], sell_date=od1[n], side=side, buy_price=buy_sp, buy_A=buy_sp, sell_price=se, days=n-buy_i, gain_pct=netpct, reason="到期平仓"}
    equity=equity*(1+netpct/100); gross_equity=gross_equity*(1+rawpct/100)
    worst=(worst==0 or netpct<worst) and netpct or worst
    sum_gain=sum_gain+netpct; sum_days=sum_days+(n-buy_i)
    if netpct>0 then wins=wins+1 end
end
local name=strat:gsub("^.*/","")
local p1=string.format("== 策略 %-28s | 3合约数据 %d 根 ==", strategy and strategy.name or name, n)
local p2=string.format("  成交 %d 笔 | 胜率 %.0f%% | 净收益 %+.1f%% | 毛收益 %+.1f%% | 最差 %+.1f%% | 平均持有 %.1f日 | 均收益 %+.1f%%", #trades, (#trades>0 and wins/#trades*100 or 0), (equity-1)*100, (gross_equity-1)*100, worst, (#trades>0 and sum_days/#trades or 0), (#trades>0 and sum_gain/#trades or 0))
local p3=string.format("  成本: 佣金 %.2f%%/单边 + 滑点 %.2f%%/单边 (毛-净=%+.1f%%)", P.commission, P.slippage, ((gross_equity-1)-(equity-1))*100)
print(p1); print(p2); print(p3)
local show=(#trades<6) and #trades or 6
for k=#trades-show+1,#trades do
    local t=trades[k]; local dirc=(t.side>0) and "做涨价差" or "做跌价差"
    print(string.format("    %s 建仓%s @%.2f -> 平仓%s @%.2f | %+.1f%% %d日 (%s)", dirc, t.date, t.buy_price, t.sell_date, t.sell_price, t.gain_pct, t.days, t.reason))
end
print("  --- 近 30 根决策轨迹 (日期 豆价 | 压榨价差 | 方向) ---")
for I=n-29,n do
    if I<2 then goto cont end
    local hs=(decided[I]==0) and "-" or string.format((decided[I]>0) and "多" or "空")
    print(string.format("    %s %8.1f | %9.1f | %s", od1[I], c1[I], spread[I], hs))
    ::cont::
end