-- RSRS + MA 择时信号 (Jack-Cherish/quantitative lesson1/quantitive-etf 移植)。
-- 原策略在 3 只 ETF 上做动量轮动选股, 并用沪深300指数做 RSRS+MA 择时, 全仓持有最优1只。
-- 本引擎为单票全仓, 只移植"择时"核心模块(BUY/SELL/KEEP); 择时信号按标的自身 OHLC 计算。
-- 参数与源码对齐: N=18(RSRS斜率窗口), MA端点两段均值比较。
strategy = { name = "RSRS+MA 择时", desc = "RSRS标准分*判定系数 与 MA 两段均值 综合" }
params = {
    { key="N",         label="RSRS斜率窗口", min=2, max=30,  step=1,  int=true,  value=18 },
    { key="M",         label="斜率序列窗口", min=10, max=620,  step=1,  int=true,  value=200 },
    { key="mean_day",  label="MA结束天数",  min=2, max=60,  step=1,  int=true,  value=20 },
    { key="mean_diff_day", label="MA初始天数", min=1, max=10,  step=1,  int=true,  value=3 },
    { key="score_threshold", label="RSRS阈值", min=0, max=3,  step=0.05,  value=0.7 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

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

local function linfit(xs, ys, n)
    local sx=0; local sy=0; local sxx=0; local sxy=0
    for i=1,n do local x=xs[i]; sx=sx+x; sy=sy+ys[i]; sxx=sxx+x*x; sxy=sxy+x*ys[i] end
    local d=n*sxx - sx*sx
    local slope=(d~=0) and (n*sxy - sx*sy)/d or 0
    local intercept=(sy - slope*sx)/n
    local mean=sy/n; local num=0; local den=0
    for i=1,n do local y=ys[i]; local pred=slope*xs[i]+intercept; num=num+(y-pred)^2; den=den+(y-mean)^2 end
    local r2=(den~=0) and (1-num/den) or 0
    return slope, intercept, r2
end

local slope_series = {}

local function get_timing_signal(ctx)
    local last=ctx.i+1
    local win=P.mean_day+P.mean_diff_day
    if last<win then return "KEEP" end
    local close=ctx.close_arr
    local todayS=0; local beforeS=0
    for i=last-P.mean_day+1,last do todayS=todayS+close[i] end
    for i=last-win+1,last-win+P.mean_diff_day-1 do beforeS=beforeS+close[i] end
    local today_MA=todayS/P.mean_day
    local before_MA=beforeS/P.mean_diff_day
    if last<P.N then return "KEEP" end
    local hi=ctx.high_arr; local lo=ctx.low_arr
    local xs={}; local ys={}
    for j=last-P.N+1,last do xs[#xs+1]=lo[j]; ys[#ys+1]=hi[j] end
    local _, slope, r2 = linfit(xs, ys, P.N)
    slope_series[#slope_series+1]=slope
    local rsrs_score=get_zscore(slope_series,P.M) * r2
    if rsrs_score>P.score_threshold and today_MA>before_MA then return "BUY" end
    if rsrs_score<-P.score_threshold and today_MA<before_MA then return "SELL" end
    return "KEEP"
end

function on_bar(ctx)
    return get_timing_signal(ctx)
end
