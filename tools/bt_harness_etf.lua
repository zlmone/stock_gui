-- ============================================================
-- bt_harness_etf.lua -- 多目标 ETF 动量轮动回测引擎
-- 用法: ./runlua bt_harness_etf.lua <策略.lua> [ETF1.csv] [ETF2.csv] [ETF3.csv] [指数.csv]
-- 策略 on_bar(ctx) 返回 0(空仓)/1/2/3(持对应ETF); 引擎负责切仓 + 净值核算。
-- 注: 本 Lua 构建单语句不支持逗号分隔的多个 locals; 表字段必须用逗号分隔。
-- ============================================================
local strat = arg[1]
if not strat then print("用法: runlua bt_harness_etf.lua <策略.lua> [ETF1] [ETF2] [ETF3] [指数]"); return end
local e1 = arg[2] or "tools/data/etf_510300.csv"
local e2 = arg[3] or "tools/data/etf_510500.csv"
local e3 = arg[4] or "tools/data/etf_159915.csv"
local idx = arg[5] or "tools/data/idx_000300.csv"

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

local od1,c1,h1,l1,o1,vol1 = loadcsv(e1)
local _,c2,h2,l2,o2,vol2 = loadcsv(e2)
local _,c3,h3,l3,o3,vol3 = loadcsv(e3)
local _,ic,ih,il,oi,vol4 = loadcsv(idx)
local n=math.min(#c1,#c2,#c3,#ic)
if n<3 then print("数据不足:", n); return end

local maxBars=(arg[6] and tonumber(arg[6]) or 1e9)
if n>maxBars then
    local s=n-maxBars
    local nc1={}; local nc2={}; local nc3={}; local nic={}; local nd={}
    for I=s+1,n do nc1[#nc1+1]=c1[I]; nc2[#nc2+1]=c2[I]; nc3[#nc3+1]=c3[I]; nic[#nic+1]=ic[I]; nd[#nd+1]=od1[I] end
    c1,c2,c3,ic,od1=nc1,nc2,nc3,nic,nd
    n=n-maxBars
end

dofile(strat)
local P={}
if params then for _,v in ipairs(params) do P[v.key]=v.value end end

local holding=0; local buy_price=0; local buy_i=0; local buy_date=""; local equity=1.0
local worst=0; local sum_gain=0; local sum_days=0; local wins=0; local trades={}
local decided={}; local sig_arr={}
for I=2,n do
    local ctx={ i=I-1, date=od1[I], day=I, close=ic[I], holding=(holding~=0), buy_price=buy_price, gain=(holding~=0) and (ic[I]-buy_price)/buy_price or 0, days=(holding~=0) and (I-buy_i) or 0, etf1_arr=c1, etf2_arr=c2, etf3_arr=c3, etf1_hi=h1, etf1_lo=l1, etf2_hi=h2, etf2_lo=l2, etf3_hi=h3, etf3_lo=l3, idx_close=ic, idx_hi=ih, idx_lo=il, i1=c1[I], i2=c2[I], i3=c3[I] }
    local target=on_bar(ctx)
    sig_arr[I]=tostring(SIGNAL or "")
    if target~=holding then
        if holding~=0 then
            local gp=(ic[I]-buy_price)/buy_price*100
            trades[#trades+1]={date=od1[I], sell_date=od1[I], buy_date=buy_date, etf=holding, buy_price=buy_price, sell_price=ic[I], days=I-buy_i, gain_pct=gp, reason=(target==0 and "择时" or "轮动")}
            equity=equity*(1+gp/100)
            worst=(worst==0 or gp<worst) and gp or worst
            sum_gain=sum_gain+gp; sum_days=sum_days+(I-buy_i)
            if gp>0 then wins=wins+1 end
            holding=0
        end
        if target~=0 then holding=target; buy_price=ic[I]; buy_i=I; buy_date=od1[I] end
    end
    decided[I]=holding
end
if holding~=0 then
    local gp=(ic[n]-buy_price)/buy_price*100
    trades[#trades+1]={date=od1[n], sell_date=od1[n], buy_date=buy_date, etf=holding, buy_price=buy_price, sell_price=ic[n], days=n-buy_i, gain_pct=gp, reason="到期平仓"}
    equity=equity*(1+gp/100)
    worst=(worst==0 or gp<worst) and gp or worst
    sum_gain=sum_gain+gp; sum_days=sum_days+(n-buy_i)
    if gp>0 then wins=wins+1 end
end

local name=strat:gsub("^.*/","")
print(string.format("== 策略 %-28s | 数据 %d 根 ==", strategy and strategy.name or name, n))
print(string.format("  成交 %d 笔 | 胜率 %.0f%% | 总收益 %+.1f%% | 最差 %+.1f%% | 平均持有 %.1f日 | 均收益 %+.1f%%",
    #trades, (#trades>0 and wins/#trades*100 or 0), (equity-1)*100, worst,
    (#trades>0 and sum_days/#trades or 0), (#trades>0 and sum_gain/#trades or 0)))
local show=(#trades<5) and #trades or 5
for k=#trades-show+1,#trades do
    local t=trades[k]
    print(string.format("    ETF%d %s 买%.2f -> %s 卖%.2f | %+.1f%% %d日 (%s)",
        t.etf, t.buy_date, t.buy_price, t.sell_date, t.sell_price, t.gain_pct, t.days, t.reason))
end
print("  --- 近 30 根决策轨迹 (日期  收盘 | 持有ETF/信号) ---")
for I=n-29,n do
    if I<2 then goto cont end
    local hsh=(decided[I]==0) and "-" or string.format("ETF%d",decided[I])
    print(string.format("    %s %8.3f | %s | %s", od1[I], ic[I], hsh, sig_arr[I] or ""))
    ::cont::
end
