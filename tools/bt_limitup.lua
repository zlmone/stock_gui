-- ============================================================
-- bt_limitup.lua -- A股"涨停后 N 天"日历效应 / 打板轮动回测引擎
-- 用法: ./runlua bt_limitup.lua [市场.csv] [N1 N2 ...] [--comm=.. --stamp=.. --slip=.. --limit=..]
-- 市场csv: code,date,open,high,low,close,volume,is_limitup,is_limitdown
-- 引擎只做数据分析与回测, 不提供投资建议。
-- ============================================================
local cost = {}
local Ns = {1,3,5,7,9}
local market = "tools/data/mkt_limitup.csv"
local saw_num=false
for a=1,#arg do
    local v = arg[a]
    if v:sub(1,2)=="--" then
        local k,VAL = v:match("^%-%-([%w_]+)=(%S+)$")
        if k then cost[k]=tonumber(VAL) or VAL end
    elseif v:match("^%d+%.?%d*$") then
        if not saw_num then Ns={tonumber(v)}; saw_num=true
        else Ns[#Ns+1]=tonumber(v) end
    else market = v end
end
local COMM  = cost.commission  or cost.comm  or 0.00025
local STAMP = cost.stamp or cost.stamp_duty or 0.0005
local SLIP  = cost.slip or cost.slippage   or 0.0002
local LIMIT = cost.limit or 0.10
local BUYC  = COMM + SLIP
local SELLC = COMM + STAMP + SLIP

local function loadcsv(path)
    local S={}; local order={}; local dayidx={}; local dates={}; local LUT={}
    for line in io.open(path,"r"):lines() do
        local f={}
        for x in line:gmatch("[^,]+") do f[#f+1]=x end
        if f[1]~="code" and f[1] and f[6] then
            local code=f[1]; local date=f[2]
            if not dayidx[date] then dayidx[date]=#dates+1; dates[#dates+1]=date end
            if not S[code] then S[code]={}; order[#order+1]=code end
            local row={ date=date, open=tonumber(f[3]), high=tonumber(f[4]),
                low=tonumber(f[5]), close=tonumber(f[6]), vol=tonumber(f[7]) or 0,
                up=(f[8]=="1") and 1 or 0, dl=(f[9]=="1") and 1 or 0 }
            S[code][#S[code]+1]=row; LUT[code.."@"..date]=row
        end
    end
    return S, order, dayidx, dates, LUT
end
local S, order, dayidx, dates, LUT = loadcsv(market)
local D = #dates
if D<2 then print("数据不足:", D); return end

local events={}
for _, code in ipairs(order) do
    local s = S[code]
    for i=1,#s do
        if s[i].up==1 then events[#events+1]={code=code, i=i, date=s[i].date, close=s[i].close} end
    end
end

local candidates={}
local n_total=#events; local n_blocked=0; local entry_days=0
for _, ev in ipairs(events) do
    local di = dayidx[ev.date]; local nds = dates[di+1]
    if nds then
        local nr = LUT[ev.code.."@"..nds]
        if nr then
            local blocked = (nr.open >= ev.close*(1+LIMIT) - 0.10) or (nr.up==1)
            if blocked then n_blocked=n_blocked+1
            else
                if candidates[di+1]==nil then candidates[di+1]={}; entry_days=entry_days+1 end
                candidates[di+1][#candidates[di+1]+1]=ev.code
            end
        end
    end
end
local tradable_days=entry_days

local function median(t)
    local s={}
    for k,v in ipairs(t) do s[#s+1]=v end
    table.sort(s); local n=#s
    if n==0 then return 0 end
    if n%2==1 then return s[(n+1)/2] else return (s[n/2]+s[n/2+1])/2 end
end
local function avg(t) local s=0
    for _,v in ipairs(t) do s=s+v end
    return #t>0 and s/#t or 0 end

print("============================================================")
print(string.format("  涨停效应回测 | 股票 %d 股 | 交易日 %d 日 | 涨停事件 %d 笔", #order, D, n_total))
print(string.format("  次日可买: %d 交易日 (%.1f%% 涨停可成交) | 一字/连板买不进: %d 笔",
    tradable_days, 100.0*(n_total-n_blocked)/n_total, n_blocked))
print(string.format("  成本: 买入+%.3f%% 卖出+%.3f%% (佣金/印花税/滑点)", BUYC*100, SELLC*100))
print("------------------------------------------------------------")
print(string.format("  %-6s | %-9s | %-13s | %-9s | %-11s",
    "持有N日","样本数","平均(含成本)","胜率","中位数"))
print("------------------------------------------------------------")
for _, N in ipairs(Ns) do
    local costs={}; local wins=0
    for _, ev in ipairs(events) do
        local j = ev.i + N
        if j <= #S[ev.code] then
            local r1 = S[ev.code][j].close*(1-SELLC)/(ev.close*(1+BUYC)) - 1
            costs[#costs+1]=r1
            if r1>0 then wins=wins+1 end
        end
    end
    print(string.format("  %-6d | %-9d | %+.3f%% | %+.1f%% | %+.3f%%",
        N, #costs, avg(costs)*100, (#costs>0 and wins/#costs*100 or 0), median(costs)*100))
end
print("------------------------------------------------------------")

local E=1e6; local cash=E; local pos={}
local curve={}; local nbatch=0; local holding=false; local exit_day=0
local Ns_main = Ns[1]
for d=1,D do
    if holding and d>=exit_day then
        local val=0
        for k=1,#pos do val=val+pos[k].basis*S[pos[k].code][dayidx[dates[d]]].close/pos[k].buy_net end
        cash=val*(1-SELLC); pos={}; holding=false
    end
    if not holding and candidates[d] and #candidates[d]>0 then
        local alloc=cash/#candidates[d]
        for _, code in ipairs(candidates[d]) do
            local o=S[code][dayidx[dates[d]]].open
            pos[#pos+1]={basis=alloc, buy_net=o*(1+BUYC), code=code}
        end
        cash=0; holding=true; exit_day=d+Ns_main; nbatch=nbatch+1
    end
    if holding then
        local val=0
        for k=1,#pos do val=val+pos[k].basis*S[pos[k].code][dayidx[dates[d]]].close/pos[k].buy_net end
        curve[d]=val
    else
        curve[d]=cash
    end
end
if holding then
    local val=0
    for k=1,#pos do val=val+pos[k].basis*S[pos[k].code][dayidx[dates[D]]].close/pos[k].buy_net end
    cash=val*(1-SELLC); nbatch=nbatch+1
end
local eq_end=cash; local peak=E; local maxdd=0
for d=1,D do
    if curve[d]>peak then peak=curve[d] end
    if curve[d]<peak and (1-curve[d]/peak)*100>maxdd then maxdd=(1-curve[d]/peak)*100 end
end
print(string.format("  打板轮动策略 | 持有期 N=%d 日 | 等权滚动满仓 | 交易周期 %d 次", Ns_main, nbatch))
print(string.format("  期末净值 %+.2f%% | 最大回撤 %.2f%% | 期末现金 %.0f",
    (eq_end-E)/E*100, maxdd, eq_end))
print("============================================================")