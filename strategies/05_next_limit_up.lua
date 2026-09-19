-- 涨停前夜 (三鬼影子): 量缩 + 横盘 + 收盘稳
-- 筛选模式: on_scan(s) → [形态筛选] 按钮 | 回测模式: on_bar(ctx) → [回测] 按钮
strategy = { name = "涨停前夜", desc = "量比缩+均线粘合+窄幅横盘+收盘上半区" }

params = {
    { key="vol_lo",    label="量比下限",     min=0.3, max=1.2, step=0.05, value=0.60 },
    { key="vol_hi",    label="量比上限",     min=0.4, max=1.5, step=0.05, value=0.85 },
    { key="amp_max",   label="振幅上限%",    min=1,   max=8,   step=0.5,  value=3.5 },
    { key="gap_max",   label="均线距上限%",  min=0.5, max=6,   step=0.5,  value=3.0 },
    { key="flat_days", label="横盘考察天数",  min=3,  max=10,  step=1, int=true, value=4 },
    { key="flat_hi",   label="区间高度上限%", min=2,  max=12,  step=0.5,  value=7.0 },
    { key="pos60_max", label="位置上限%",    min=30,  max=100, step=5,    value=80 },
    { key="tp",        label="回测止盈%",    min=2,   max=15,  step=0.5,  value=5 },
    { key="hold_max",  label="回测持有日",   min=1,   max=10,  step=1, int=true, value=3 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

-- 形态判断(共用核心): getter 函数取第 k 根数值
local function shape_ok(n, go, gh, gl, gc, gv, gm5, gm10)
    if n < 15 then return false end
    local vsum = 0
    for k = n-5, n-1 do vsum = vsum + gv(k) end
    local vr = gv(n) / (vsum / 5)
    if vr < P.vol_lo or vr > P.vol_hi then return false end
    local amp = (gh(n) - gl(n)) / gc(n-1) * 100
    if amp > P.amp_max then return false end
    local ma5, ma10 = gm5(n), gm10(n)
    if ma5 <= 0 or ma10 <= 0 then return false end
    if math.abs(ma5 - ma10) / ma10 * 100 > P.gap_max then return false end
    if ma5 < ma10 * 0.999 then return false end
    local hi, lo = gh(n-P.flat_days+1), gl(n-P.flat_days+1)
    for k = n-P.flat_days+1, n do
        if gh(k) > hi then hi = gh(k) end
        if gl(k) < lo then lo = gl(k) end
    end
    if (hi - lo) / lo * 100 > P.flat_hi then return false end
    local pct = (gc(n) - gc(n-1)) / gc(n-1) * 100
    if pct < -2.0 or pct > 4.0 then return false end
    if gc(n) < (gh(n) + gl(n)) / 2 then return false end
    return true, vr, amp
end

function on_scan(s)
    local n = s.n
    local ok, vr, amp = shape_ok(n,
        function(k) return s.open[k] end,
        function(k) return s.high[k] end,
        function(k) return s.low[k] end,
        function(k) return s.close[k] end,
        function(k) return s.vol[k] end,
        function(k) return s.ma5[k] or 0 end,
        function(k) return s.ma10[k] or 0 end)
    if not ok then return false end
    local whi, wlo = s.high[1], s.low[1]
    for i = 1, n do
        if s.high[i] > whi then whi = s.high[i] end
        if s.low[i] < wlo then wlo = s.low[i] end
    end
    local pos = (s.close[n] - wlo) / (whi - wlo) * 100
    if pos > P.pos60_max then return false end
    return true, string.format("量比%.2f 振幅%.1f%% 位置%.0f%%", vr, amp, pos)
end

function on_bar(ctx)
    local i = ctx.i
    if i < 16 then return "HOLD" end
    if not ctx.holding then
        local ok = shape_ok(i,
            function(k) return ctx.open_arr[k] end,
            function(k) return ctx.high_arr[k] end,
            function(k) return ctx.low_arr[k] end,
            function(k) return ctx.close_arr[k] end,
            function(k) return ctx.vol_arr[k] end,
            function(k) return ctx.ma5_arr[k] end,
            function(k) return ctx.ma10_arr[k] end)
        if ok then return "BUY" end
    else
        if ctx.gain >= P.tp / 100 then return "SELL" end
        if ctx.days >= P.hold_max then return "SELL" end
    end
    return "HOLD"
end
