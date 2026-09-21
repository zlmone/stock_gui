-- ============================================================
-- bt_harness_grid.lua -- 单合约网格交易回测引擎(带余额/双向网格状态)
-- 用法: ./runlua bt_harness_grid.lua <策略.lua> [合约.csv]
-- 策略于 on_bar 内设置全局目标持仓 TARGET_VOL(整数手数, 正多负空)
-- 引擎负责档位缩放开平: 买入记entry(含滑点), 卖出兑现盈亏(扣佣金/滑点), 并校验保证金。
-- 净值 equity = cash + 未实现盈亏; 新增敞口受剩余保证金约束。
-- ============================================================
local strat = arg[1]
if not strat then print("用法: runlua bt_harness_grid.lua <策略.lua> [合约.csv]"); return end
local f = arg[2] or "tools/data/fut_synth.csv"
local function loadcsv(path)
    local cl={}; local hi={}; local lo={}; local od={}
    for line in io.open(path,"r"):lines() do
        local f={}
        for x in line:gmatch("[^,]+") do f[#f+1]=x end
        if #f>=5 then
            od[#od+1]=f[1]; cl[#cl+1]=tonumber(f[5]); hi[#hi+1]=tonumber(f[3]); lo[#lo+1]=tonumber(f[4])
        end
    end
    return od,cl,hi,lo
end
local od,c1,h1,l1 = loadcsv(f); local n=#c1
if n<2 then print("数据不足:", n); return end
local maxBars=(arg[3] and tonumber(arg[3]) or 1e9)
if maxBars>0 and n>maxBars then n=maxBars end
dofile(strat)
local P={}
if params then for _,v in ipairs(params) do P[v.key]=v.value end end
P.start_capital=P.start_capital or 1000000
P.margin=P.margin or 0.1
P.commission=P.commission or 0.02
P.slippage=P.slippage or 0.05
if arg then for a=3,#arg do local k,v=arg[a]:match("^(.+)=(.+)$") if k then P[k]=tonumber(v) or v end end end
local position=0; local long_e={}; local short_e={}; local cash=P.start_capital; local realized=0
local closes={}; local decided={}; local target_hist={}; local equity_hist={}
for I=1,n do
    TARGET_VOL=0
    local price=c1[I]
    local unreal=0
    for k=1,#long_e do unreal=unreal+(price-long_e[k]) end
    for k=1,#short_e do unreal=unreal+(short_e[k]-price) end
    local equity=cash+unreal
    local ctx={ i=I-1, date=od[I], close=c1[I], close_arr=c1, hi=h1, lo=l1,
        position=position, cash=cash, equity=equity, open_lots=#long_e+#short_e }
    local _=on_bar(ctx)
    local target=TARGET_VOL or 0
    if target~=position then
        if target>position then
            local delta=target-position; local close=0
            if #short_e>0 then
                close=math.min(delta,#short_e)
                for j=1,close do local e=table.remove(short_e); local pnl=price*(1-P.slippage/100)-e-price*P.commission/100; realized=realized+pnl; cash=cash+pnl; closes[#closes+1]=pnl end
                delta=delta-close
            end
            local cap=math.floor(equity/(price*P.margin))
            if cap<delta then delta=cap end
            for j=1,delta do long_e[#long_e+1]=price*(1+P.slippage/100); cash=cash-price*P.commission/100 end
            position=position+close+delta
        else
            local delta=position-target; local close=0
            if #long_e>0 then
                close=math.min(delta,#long_e)
                for j=1,close do local e=table.remove(long_e); local pnl=price*(1-P.slippage/100)-e-price*P.commission/100; realized=realized+pnl; cash=cash+pnl; closes[#closes+1]=pnl end
                delta=delta-close
            end
            for j=1,delta do short_e[#short_e+1]=price*(1+P.slippage/100); cash=cash-price*P.commission/100 end
            position=position-close-delta
        end
    end
    local u=0; for k=1,#long_e do u=u+(price-long_e[k]) end; for k=1,#short_e do u=u+(short_e[k]-price) end
    decided[I]=position; target_hist[I]=target; equity_hist[I]=cash+u
end
local final_long=#long_e; local final_short=#short_e
if #long_e>0 or #short_e>0 then
    local price=c1[n]
    for k=1,#long_e do local e=table.remove(long_e); local pnl=price*(1-P.slippage/100)-e-price*P.commission/100; realized=realized+pnl; cash=cash+pnl; closes[#closes+1]=pnl end
    for k=1,#short_e do local e=table.remove(short_e); local pnl=e-price*(1-P.slippage/100)-price*P.commission/100; realized=realized+pnl; cash=cash+pnl; closes[#closes+1]=pnl end
    position=0
end
local net=(cash-1e6)/1e6*100
local gross=realized/1e6*100
local wins=0; for _,c in ipairs(closes) do if c>0 then wins=wins+1 end end
local minE=1e18
for I=1,n do if equity_hist[I]<minE then minE=equity_hist[I] end end
local drawdown=(minE<1e17) and (1-equity_hist[n]/minE)*100 or 0
local name=strat:gsub("^.*/","")
local p1=string.format("== 策略 %-24s | 网格数据 %d 根 ==", strategy and strategy.name or name, n)
local p2=string.format("  净值 %.2f%% | 累计盈亏 %+.0f | 已平仓 %d 档 胜率 %.0f%% | 最大回撤 %.1f%% | 均每档 %+.1f", net, realized, #closes, (#closes>0 and wins/#closes*100 or 0), drawdown, (#closes>0 and realized/#closes or 0))
local p3=string.format("  期末持仓: 0 手 | 现金 %.1f | 未实现 %+.0f | 多头档 %d | 空头档 %d", cash, (equity_hist[n]-cash), final_long, final_short)
print(p1); print(p2); print(p3)
local show=(#closes<10) and #closes or 10
for k=#closes-show+1,#closes do
    print(string.format("    平仓第%d档盈亏 %+.1f 元 (%s)", k, closes[k], closes[k]>0 and "盈" or "亏"))
end
print("  --- 近 20 根决策轨迹 (日期  收盘 | 目标持仓 | 实际持仓) ---")
for I=n-19,n do
    if I<1 then goto cont end
    print(string.format("    %s %8.1f | %6d | %6d", od[I], c1[I], target_hist[I], decided[I]))
    ::cont::
end