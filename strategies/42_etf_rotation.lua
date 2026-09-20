-- 3-ETF 动量轮动 + RSRS/MA 择时 (Jack-Cherish/quantitative lesson1/quantitive-etf 完整移植)。
-- 原策略: 3 只ETF(创业板/沪深300/中证500) 每日用"年化收益*判定系数"动量因子打分, 取最优 stock_num 只持仓;
--        并用沪深300指数做 RSRS标准分*判定系数 与 MA 两段均值 综合择时(BUY/SELL/KEEP), 全仓持有最优1只;
--        含 -20% 止损。本引擎通过 bt_harness_etf.lua 多目标引擎实现该轮动。
-- 引擎约定: 策略 on_bar(ctx) 返回 0(空仓)/1/2/3(持对应ETF); 引擎负责切仓与净值核算。
strategy = { name = "ETF 动量轮动", desc = "动量因子选股+RSRS/MA择时, 全仓持有最优1只" }
params = {
    { key="momentum_day",   label="动量天数", min=2, max=60,  step=1,  int=true,  value=29 },
    { key="N",              label="RSRS斜率窗口", min=2, max=30,  step=1,  int=true,  value=18 },
    { key="M",              label="斜率序列窗口", min=10, max=800,  step=1,  int=true,  value=600 },
    { key="warmup",         label="起步斜率数", min=10, max=200,  step=1,  int=true,  value=60 },
    { key="mean_day",       label="MA结束天数", min=2, max=60,  step=1,  int=true,  value=20 },
    { key="mean_diff_day",  label="MA初始天数", min=1, max=10,  step=1,  int=true,  value=3 },
    { key="score_threshold",label="RSRS阈值", min=0, max=3,  step=0.05,  value=0.7 },
    { key="stop_loss",      label="止损线%", min=0, max=60,  step=1,  value=20 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local math_log=math.log; local math_exp=math.exp

local function linfit(xs, ys, n)
    local sx=0; local sy=0; local sxx=0; local sxy=0
    for i=1,n do local x=xs[i]; sx=sx+x; sy=sy+ys[i]; sxx=sxx+x*x; sxy=sxy+x*ys[i] end
    local d=n*sxx - sx*sx
    local slope=(d~=0) and (n*sxy - sx*sy)/d or 0
    local intercept=(sy - slope*sx)/n
    local mean=sy/n; local num=0; local den=0
    for i=1,n do local y=ys[i]; local pred=slope*xs[i]+intercept; num=num+(y-pred)^2; den=den+(y-mean)^2 end
    return slope, intercept, (den~=0) and (1-num/den) or 0
end

local function etf_score(arr, from, nn)
    local xs={}; local ys={}
    for j=from-nn+1,from do local k=j-from+nn-1; xs[#xs+1]=k; ys[#ys+1]=math_log(arr[j]) end
    local slope,_,r2 = linfit(xs, ys, nn)
    return (math_exp(slope*250) - 1) * r2
end

local function get_zscore(series, m)
    local n=#series; local w=(m<n) and m or n
    if w<1 then return 0 end
    local from=n-w+1; local s=0
    for i=from,n do s=s+series[i] end
    local mean=s/w; local v=0
    for i=from,n do v=v+(series[i]-mean)^2 end
    v=v/(w-1)
    local std=math.sqrt(v)
    if std==0 then return 0 end
    return (series[n]-mean)/std
end

local slope_series = {}

local function best_etf(ctx)
    local last=ctx.i+1
    local best=1; local best_score=-math.huge
    local arrs={ctx.etf1_arr, ctx.etf2_arr, ctx.etf3_arr}
    for k=1,3 do
        if last>=P.momentum_day then
            local sc=etf_score(arrs[k], last, P.momentum_day)
            if sc>best_score then best_score=sc; best=k end
        end
    end
    return best
end

function on_bar(ctx)
    if ctx.holding and (ctx.gain*100) <= -P.stop_loss then SIGNAL="止损"; return 0 end
    local last=ctx.i+1
    if last<P.momentum_day then SIGNAL="等待"; return 0 end
    local r2=0
    if last>=P.N then
        local xs={}; local ys={}
        for j=last-P.N+1,last do local k=j-last+P.N-1; xs[#xs+1]=ctx.idx_lo[j]; ys[#ys+1]=ctx.idx_hi[j] end
        local _,slope,r2 = linfit(xs, ys, P.N)
        slope_series[#slope_series+1]=slope
    end
    if #slope_series < P.warmup then SIGNAL="起步"; return 0 end
    local ic=ctx.idx_close
    local win=P.mean_day+P.mean_diff_day
    local todayS=0; local beforeS=0
    for i=last-P.mean_day+1,last do todayS=todayS+ic[i] end
    for i=last-win+1,last-win+P.mean_diff_day-1 do beforeS=beforeS+ic[i] end
    local today_MA=todayS/P.mean_day
    local before_MA=beforeS/P.mean_diff_day
    local rsrs_score=get_zscore(slope_series,P.M) * r2
    if rsrs_score>P.score_threshold and today_MA>before_MA then SIGNAL="BUY(择时)"; return best_etf(ctx) end
    if rsrs_score<-P.score_threshold and today_MA<before_MA then SIGNAL="SELL(择时)"; return 0 end
    SIGNAL="KEEP(择时)"; return best_etf(ctx)
end
