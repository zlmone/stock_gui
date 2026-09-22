-- 三句话选股 (复刻"5500只股票三句话搞定"的前两句)
--   ① 删掉"在跌"的   : 20日均线方向向下 -> 剔除(这一刀按原文能筛掉约 80%)
--   ② 删掉"散户太多"的: RPS(全市场相对强度百分位) < 80 -> 剔除(再筛掉约 90%)
--   ③ "看不懂的删掉" : 属于主观判断(行业/业务/管理层), 且本地库里没有财务报表
--                      数据(净利润/现金流), 无法自动化 -> 交给人工, 本策略不做
--
-- 关于 RPS: 它是"横截面"指标(该股涨幅在全市场的排名百分位), 单只股票的
--   on_scan 自己算不出来。由引擎在扫描前用一条 SQL 算好全市场 120 日涨幅的
--   百分位, 再逐只注入到 s.rps。若引擎没提供 RPS(s.rps 为 nil), 本策略会
--   自动降级为"只做第一句", 不会误杀。
--
-- 用法: 策略下拉选"三句话选股" -> 点【形态筛选 (全市场选股)】/ F7
--   回测模式(on_bar)也用同一套条件: 满足时买入, 破 MA20 或到期/止盈卖出。

strategy = { name = "三句话选股", desc = "MA20方向向上 + RPS>=80(全市场前20%) + 站上MA20" }

params = {
    { key="slope_days",  label="MA20斜率考察天数", min=3,  max=20, step=1,   int=true, value=5 },
    { key="slope_min",   label="MA20斜率下限%",    min=-1, max=2,  step=0.05, value=0.0 },
    { key="rps_min",     label="RPS下限",          min=0,  max=99, step=1,   int=true, value=80 },
    { key="need_ma20",   label="必须站上MA20(1/0)", min=0,  max=1,  step=1,   int=true, value=1 },
    { key="rps_days",    label="RPS天数(说明用)",   min=60, max=250,step=10,  int=true, value=120 },
    { key="tp",          label="回测止盈%",        min=2,  max=15, step=0.5,  value=5 },
    { key="hold_max",    label="回测持有日",       min=1,  max=10, step=1,   int=true, value=5 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

-- 共用判断: 返回 ok, 备注
local function three_ok(s)
    local n = s.n
    if n < 30 then return false, "样本不足" end

    local d = P.slope_days
    local ma_now, ma_old = s.ma20[n], s.ma20[n - d]
    if not ma_now or not ma_old then return false, "MA20不足" end

    -- ① 20日均线方向向下 -> 删掉(不看今天涨没涨, 只看趋势)
    local slope = (ma_now - ma_old) / ma_old * 100.0
    if slope < P.slope_min then
        return false, string.format("MA20向下 %.2f%%", slope)
    end

    -- ② RPS 低于下限 -> 删掉(连全市场前20%都进不了)
    local rps = s.rps
    if rps and rps > 0 and rps < P.rps_min then
        return false, string.format("RPS %.0f < %.0f", rps, P.rps_min)
    end

    -- 附加: 要求站上 MA20
    if P.need_ma20 >= 0.5 and s.close[n] < ma_now then
        return false, "未站上MA20"
    end

    if rps and rps > 0 then
        return true, string.format("MA20 %+.2f%% RPS %.0f", slope, rps)
    end
    return true, string.format("MA20 %+.2f%% (无RPS)", slope)
end

function on_scan(s)
    return three_ok(s)
end

-- 回测: 条件成立买入; 止盈 / 到期 / 跌破MA20 卖出。
-- 注意: on_bar 只拿得到"当前 bar"的 ma20(没有 MA20 历史数组),
--       所以回测里用"站上MA20 + 当日红盘"近似第一句;
--       严格的 MA20 斜率判断只在 on_scan(全市场筛选)里做。
function on_bar(ctx)
    if ctx.close == nil or ctx.ma20 == nil then return "HOLD" end
    if not ctx.holding then
        if ctx.close > ctx.ma20 and ctx.pct > 0 then return "BUY" end
    else
        if ctx.gain and ctx.gain * 100 >= P.tp then return "SELL" end
        if ctx.days and ctx.days >= P.hold_max then return "SELL" end
        if ctx.close < ctx.ma20 then return "SELL" end
    end
    return "HOLD"
end
