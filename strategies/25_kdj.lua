-- KDJ 随机指标(K=slowk, D=slowd): 跌破超卖线且 K 上穿越 D 时买入, 反之卖出。
-- 来源: ea-python KDJ指标策略.py; fastk9/slowk3/slowd3。
strategy = { name = "KDJ 随机", desc = "D<超卖线 + K上穿D 买, D>超买线 + K下穿D 卖" }
params = {
    { key="fastk_period", label="快速K周期", min=2, max=20,  step=1,  int=true,  value=9 },
    { key="slowk_period", label="慢速K周期", min=1, max=10,  step=1,  int=true,  value=3 },
    { key="slowd_period", label="慢速D周期", min=1, max=10,  step=1,  int=true,  value=3 },
    { key="over_buy",  label="超买线", min=50, max=100,  step=1,  value=80 },
    { key="over_sell", label="超卖线", min=0, max=50,  step=1,  value=20 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_min(a,f,t) local m=math.huge for i=f,t do if a[i]<m then m=a[i] end end return m end
local function arr_max(a,f,t) local m=-math.huge for i=f,t do if a[i]>m then m=a[i] end end return m end
local function arr_mean(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s/(t-f+1) end
local function calcKDJ(high, low, close, fp, sk, sd)
    local n = #close
    local rsv = {}; local fastk = {}; local K = {}; local D = {}
    for i=1,n do rsv[i]=0; fastk[i]=0; K[i]=0; D[i]=0 end
    for i=1,n do
        if i>=fp then
            local hn = arr_max(high, i-fp+1, i); local ln = arr_min(low, i-fp+1, i)
            rsv[i] = (hn>ln) and (close[i]-ln)/(hn-ln)*100 or 0
        end
    end
    for i=1,n do
        if i>=sk then fastk[i]=arr_mean(rsv, i-sk+1, i) end
        if i>=sk then K[i]=arr_mean(fastk, i-sk+1, i) end
        if i>=sd then D[i]=arr_mean(K, i-sd+1, i) end
    end
    return K, D
end
function on_bar(ctx)
    local last = ctx.i + 1
    if last<P.fastk_period then return "HOLD" end
    local K, D = calcKDJ(ctx.high_arr, ctx.low_arr, ctx.close_arr, P.fastk_period, P.slowk_period, P.slowd_period)
    local k = K[last]; local d = D[last]
    local kp = (last>1) and K[last-1] or 0; local dp = (last>1) and D[last-1] or 0
    if not ctx.holding then
        if d < P.over_sell and d > dp and k > kp and kp < dp and k > d then return "BUY" end
        return "HOLD"
    else
        if d > P.over_buy and d < dp and k < kp and kp > dp and k < d then return "SELL" end
        return "HOLD"
    end
end
