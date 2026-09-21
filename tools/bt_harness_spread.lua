-- ============================================================
-- bt_harness_spread.lua -- 两合约(可选三合约)价差均值回归回测引擎
-- 用法: ./runlua bt_harness_spread.lua <策略.lua> [合约A.csv] [合约B.csv] [合约C.csv]
-- 策略 on_bar 返回 "BUY"(买入价差 = 买A卖B)/"SELL"(卖出价差)/"HOLD"(空仓)
-- 引擎内部以 A-B 差价差为结算标的, 成交收益 = 方向*(差价差变化)/建仓时A价格
-- 每次开平单边扣 commission+slippage; 策略可用 ctx.gain 做 A/B 结算的盈亏% 判断。
-- ============================================================
local strat = arg[1]
if not strat then print("用法: runlua bt_harness_spread.lua <策略.lua> [合约A] [合约B]"); return end
local f1 = arg[2] or "tools/data/sym_cu.csv"
local f2 = arg[3] or "tools/data/sym_al.csv"
local hasC = arg[4] ~= nil
local function loadcsv(path)
    local op={}; local hi={}; local lo={}; local cl={}; local vol={}; local od={}
    for line in io.open(path,"r"):lines() do
        local f={}
        for x in line:gmatch("[^,]+") do f[#f+1]=x end
        if #f>=6 then
            od[#od+1]=f[1]
            cl[#cl+1]=tonumber(f[5]); hi[#hi+1]=tonumber(f[3]); lo[#lo+1]=tonumber(f[4]); op[#op+1]=tonumber(f[2]); vol[#vol+1]=tonumber(f[6])
        end
    end
    return od,cl,hi,lo,op,vol
end
local od1,c1,h1,l1,o1,vol1 = loadcsv(f1)
local _,c2,h2,l2,o2,vol2 = loadcsv(f2)
local c3=nil
if hasC then local _,c3_,_,_,_,_ = loadcsv(arg[4]); c3=c3_ end
local n=math.min(#c1,#c2)
if hasC then n=math.min(n,#c3) end
if n<3 then print("数据不足:", n); return end
local diff={}; local ratio={}
for i=1,n do diff[i]=c1[i]-c2[i]; ratio[i]=c1[i]/c2[i] end
local maxBars=(arg[5] and tonumber(arg[5]) or 1e9)
if maxBars>0 and n>maxBars then
    local s=n-maxBars
    local nc1={}; local nc2={}; local nh1={}; local nl1={}; local nov={}; local nod={}
    for I=s+1,n do nc1[#nc1+1]=c1[I]; nc2[#nc2+1]=c2[I]; nh1[#nh1+1]=h1[I]; nl1[#nl1+1]=l1[I]; nov[#nov+1]=vol1[I]; nod[#nod+1]=od1[I] end
    c1,c2,h1,l1,vol1,od1=nc1,nc2,nh1,nl1,nov,nod
    n=n-maxBars
end
dofile(strat)
local P={}
if params then for _,v in ipairs(params) do P[v.key]=v.value end end
P.commission=P.commission or 0.1
P.slippage=P.slippage or 0.1
if arg then for a=6,#arg do local k,v=arg[a]:match("^(.+)=(.+)$") if k then P[k]=tonumber(v) or v end end end
local side=0; local buy_i=0; local buy_date=""; local buy_sp=0; local buy_A=0
local equity=1.0; local gross_equity=1.0; local worst=0
local sum_gain=0; local sum_days=0; local wins=0; local trades={}
local decided={}; local sig_arr={}
for I=2,n do
    local ctx={ i=I-1, date=od1[I], day=I, close=c1[I], close2=c2[I],
        holding=(side~=0), buy_price=buy_sp, buy2=c2[buy_i],
        gain=(side~=0) and side*(diff[I]-buy_sp)/math.max(math.abs(buy_A),1) or 0,
        days=(side~=0) and (I-buy_i) or 0,
        a_arr=c1, b_arr=c2, c_arr=(hasC and c3) or nil,
        diff_arr=diff, ratio_arr=ratio,
        a_hi=h1, a_lo=l1, b_hi=h2, b_lo=l2, vol_arr=vol1, i1=c1[I], i2=c2[I] }
    local sig=on_bar(ctx)
    sig_arr[I]=tostring(SIGNAL or "")
    local desired=(sig=="BUY") and 1 or (sig=="SELL") and -1 or 0
    if desired~=side then
        if side~=0 then
            local se=diff[I]
            local rawpct=side*(se-buy_sp)/math.max(math.abs(buy_A),1)*100
            local feePct=P.commission+P.slippage
            local netpct=rawpct-feePct
            trades[#trades+1]={date=od1[buy_i], sell_date=od1[I], side=side, buy_price=buy_sp, buy_A=buy_A, sell_price=se, days=I-buy_i, gain_pct=netpct, reason=(sig=="HOLD" and "HOLD" or "切换")}
            equity=equity*(1+netpct/100); gross_equity=gross_equity*(1+rawpct/100)
            worst=(worst==0 or netpct<worst) and netpct or worst
            sum_gain=sum_gain+netpct; sum_days=sum_days+(I-buy_i)
            if netpct>0 then wins=wins+1 end
        end
        if desired~=0 then buy_i=I; buy_date=od1[I]; buy_sp=diff[I]; buy_A=c1[I] end
        side=desired
    end
    decided[I]=side
end
if side~=0 then
    local se=diff[n]
    local rawpct=side*(se-buy_sp)/math.max(math.abs(buy_A),1)*100
    local feePct=P.commission+P.slippage
    local netpct=rawpct-feePct
    trades[#trades+1]={date=od1[buy_i], sell_date=od1[n], side=side, buy_price=buy_sp, buy_A=buy_A, sell_price=se, days=n-buy_i, gain_pct=netpct, reason="到期平仓"}
    equity=equity*(1+netpct/100); gross_equity=gross_equity*(1+rawpct/100)
    worst=(worst==0 or netpct<worst) and netpct or worst
    sum_gain=sum_gain+netpct; sum_days=sum_days+(n-buy_i)
    if netpct>0 then wins=wins+1 end
end
local name=strat:gsub("^.*/","")
local p1=string.format("== 策略 %-28s | 数据 %d 根 ==", strategy and strategy.name or name, n)
local p2=string.format("  成交 %d 笔 | 胜率 %.0f%% | 净收益 %+.1f%% | 毛收益 %+.1f%% | 最差 %+.1f%% | 平均持有 %.1f日 | 均收益 %+.1f%%", #trades, (#trades>0 and wins/#trades*100 or 0), (equity-1)*100, (gross_equity-1)*100, worst, (#trades>0 and sum_days/#trades or 0), (#trades>0 and sum_gain/#trades or 0))
local p3=string.format("  成本: 佣金 %.1f%%/单边 + 滑点 %.1f%%/单边 (毛-净=%+.1f%%)", P.commission, P.slippage, ((gross_equity-1)-(equity-1))*100)
print(p1); print(p2); print(p3)
local show=(#trades<5) and #trades or 5
for k=#trades-show+1,#trades do
    local t=trades[k]
    local dirc=(t.side>0) and "做多" or "做空"
    print(string.format("    价差%s 建仓%s @%.2f -> 平仓%s @%.2f | %+.1f%% %d日 (%s)", dirc, t.date, t.buy_price, t.sell_date, t.sell_price, t.gain_pct, t.days, t.reason))
end
print("  --- 近 30 根决策轨迹 (日期 收盘A | 价差A-B | 方向) ---")
for I=n-29,n do
    if I<2 then goto cont end
    local hs=(decided[I]==0) and "-" or string.format((decided[I]>0) and "多" or "空")
    print(string.format("    %s %10.2f | %8.1f | %s", od1[I], c1[I], diff[I], hs))
    ::cont::
end