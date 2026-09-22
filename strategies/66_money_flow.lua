-- 主力资金流选股 (当日)
--   数据来自"东财资金流排行", 由 菜单 数据 → 更新当日资金流 抓取落库(fundflow.db)。
--   扫描时引擎把当日资金流逐只注入到 s:
--     s.main_net   主力净流入(万元)   = 超大单 + 大单
--     s.main_pct   主力净占比(%)      = 净额 / 成交额   ← 比绝对额公平, 不被大市值股刷屏
--     s.super_net / s.big_net / s.mid_net / s.small_net  四类单净额(万元)
--     s.ff_date    资金流数据日期
--   注意: 资金流只有"当日"一个截面(本地库不存历史), 所以本策略只做 on_scan 选股;
--         回测(on_bar)拿不到历史资金流, 用"放量 + 站上MA20"近似, 仅作形态参考。
--
-- 用法: 先 数据 → 更新当日资金流, 再选本策略 → F7 形态筛选

strategy = { name = "主力资金流", desc = "当日主力净流入 + 净占比达标 + 涨幅温和 + 站上MA20" }

params = {
    { key="min_net",   label="主力净流入下限(万)", min=-100000, max=500000, step=500,  value=3000 },
    { key="min_pct",   label="主力净占比下限%",    min=-20,     max=50,     step=0.5,  value=3.0 },
    { key="min_chg",   label="涨幅下限%",          min=-15,     max=10,     step=0.5,  value=-2.0 },
    { key="max_chg",   label="涨幅上限%",          min=0,       max=20,     step=0.5,  value=7.0 },
    { key="need_ma20", label="必须站上MA20(1/0)",  min=0,       max=1,      step=1, int=true, value=1 },
    { key="vol_mult",  label="回测放量倍数",       min=1,       max=5,      step=0.1,  value=1.5 },
    { key="tp",        label="回测止盈%",          min=2,       max=15,     step=0.5,  value=6 },
    { key="hold_max",  label="回测持有日",         min=1,       max=10,     step=1, int=true, value=5 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_scan(s)
    local n = s.n
    if n < 30 then return false, "样本不足" end

    -- 没有资金流数据就直接说明原因(比默默不选更不容易让人误会)
    if s.main_net == nil then
        return false, "无资金流数据(先: 数据→更新当日资金流)"
    end

    local chg = s.pct[n] or 0
    if s.main_net < P.min_net then
        return false, string.format("主力净流入 %.0f万 < %.0f万", s.main_net, P.min_net)
    end
    if s.main_pct < P.min_pct then
        return false, string.format("净占比 %.2f%% < %.2f%%", s.main_pct, P.min_pct)
    end
    if chg < P.min_chg then
        return false, string.format("跌幅 %.2f%% 过大", chg)
    end
    if chg > P.max_chg then
        return false, string.format("涨幅 %.2f%% 已追高", chg)
    end
    -- 涨停/接近涨停一票否决: 资金流再漂亮也买不到(或次日高开走人)
    if chg >= 9.5 then return false, "已涨停" end

    local ma20 = s.ma20[n]
    if P.need_ma20 >= 0.5 then
        if not ma20 then return false, "MA20不足" end
        if s.close[n] < ma20 then return false, "未站上MA20" end
    end

    return true, string.format("主力 %+.0f万(%.1f%%) 涨跌 %+.1f%%",
                               s.main_net, s.main_pct, chg)
end

-- 回测: 没有历史资金流(本地只存当日截面), 用"放量 + 站上MA20"近似做多, 仅供形态参考
function on_bar(ctx)
    if ctx.close == nil or ctx.ma20 == nil then return "HOLD" end
    local v = ctx.vol_arr and ctx.vol_arr[ctx.i]
    local v5 = 0
    if ctx.vol_arr and ctx.i and ctx.i > 5 then
        for k = ctx.i - 5, ctx.i - 1 do v5 = v5 + (ctx.vol_arr[k] or 0) end
        v5 = v5 / 5
    end
    if not ctx.holding then
        if ctx.close > ctx.ma20 and ctx.pct > 0 and v5 > 0 and v >= v5 * P.vol_mult then
            return "BUY"
        end
    else
        if ctx.gain and ctx.gain * 100 >= P.tp then return "SELL" end
        if ctx.days and ctx.days >= P.hold_max then return "SELL" end
        if ctx.close < ctx.ma20 then return "SELL" end
    end
    return "HOLD"
end
