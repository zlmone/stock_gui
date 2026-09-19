-- 机构潜伏池: 机构吸筹中、股价还没拉升 (跟着机构买)
-- 算法移植自 scan_hidden.py:
--   主力动能 = 涨跌幅% x 成交额近似(vol x close) -> EMA5 >= 0 连续红柱
--   抢筹区间 = 连续红柱天数, 期间需有放量阳线(量 >= 1.2x5日均量 且 收阳)
--   未拉升   = 抢筹区间涨幅 <= 12% (还没涨上去)
--   位置低   = 距60日低点涨幅 <= 25% (启动初期, 不是高位)
--   有空间   = 距60日高点还有 >= 10% 空间
strategy = {
    name = "机构潜伏池",
    desc = "连续红柱+放量吸筹+未拉升+低位, 适合跟随建仓"
}

-- ===== 可调参数 (界面渲染) =====
params = {
    { key="streak_min",  label="连续红柱≥(天)",  min=1,  max=10, step=1, int=true, value=3 },
    { key="max_gain",    label="区间涨幅≤%",     min=3,  max=30, step=1,    value=12 },
    { key="max_lo_pos",  label="距低点涨幅≤%",   min=5,  max=50, step=5,    value=25 },
    { key="min_headroom", label="距高点空间≥%",  min=2,  max=30, step=1,    value=10 },
    { key="min_price",   label="最低价格",       min=1,  max=30, step=1,    value=3 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_scan(s)
    local n = s.n
    if n < 60 then return false end
    if s.close[n] < P.min_price then return false end

    local lo = n - 59   -- 最近60根窗口

    -- 主力动能 EMA5 (power 近似 = 涨跌幅% * 成交量 * 收盘价)
    local alpha = 2.0 / 6.0
    local ema = {}
    ema[lo] = s.pct[lo] * s.vol[lo] * s.close[lo]
    for i = lo + 1, n do
        local p = s.pct[i] * s.vol[i] * s.close[i]
        ema[i] = alpha * p + (1 - alpha) * ema[i-1]
    end

    -- 连续红柱 (EMA >= 0, 从最后往前数)
    local streak = 0
    local i = n
    while i >= lo and ema[i] >= 0 do
        streak = streak + 1
        i = i - 1
    end
    if streak < P.streak_min then return false end
    local start = n - streak + 1
    if start <= lo then start = lo + 1 end

    -- 5日均量辅助
    local function ma5v(k)
        if k < lo + 4 then return 0 end
        local sum = 0
        for j = k - 4, k do sum = sum + s.vol[j] end
        return sum / 5
    end

    -- 抢筹区间: 放量阳线计数
    local yang = 0
    for j = start, n do
        local m = ma5v(j)
        if m > 0 and s.close[j] > s.open[j] and s.vol[j] >= 1.2 * m then
            yang = yang + 1
        end
    end

    -- 未拉升: 区间涨幅 (起点前一天收盘 -> 现价)
    local base = (start > lo) and s.close[start - 1] or s.close[start]
    if base <= 0 then return false end
    local gain = (s.close[n] - base) / base * 100
    if gain > P.max_gain then return false end

    -- 60日位置 (收盘价极值, 与原算法一致)
    local hi, low = s.close[lo], s.close[lo]
    for k = lo, n do
        if s.close[k] > hi then hi = s.close[k] end
        if s.close[k] < low then low = s.close[k] end
    end
    local now = s.close[n]
    local loPos = (now - low) / low * 100
    local headroom = (hi - now) / hi * 100
    if loPos > P.max_lo_pos then return false end
    if headroom < P.min_headroom then return false end

    return true, string.format("红柱%d天 区间%+.1f%% 放量%d次 低点上%.0f%% 空间%.0f%%",
                               streak, gain, yang, loPos, headroom)
end
