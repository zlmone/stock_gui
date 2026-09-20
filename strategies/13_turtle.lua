-- 海龟交易: 收盘突破 T 日唐奇安上轨买入, 跌破 (T/2) 日唐奇安下轨离场。
-- 来源: ea-python "海龟策略.py"; 加仓与 ATR 止损在本平台单仓位(全进全出)模型下简化为仅进出两态。
-- 数组约定: close_arr/high_arr/low_arr 1 索引, 当前根 ctx.i 对应 bars[i]。
strategy = { name = "海龟突破", desc = "突破20日新高买入, 跌破10日低点离场" }

params = {
    { key="T", label="唐奇安天数", min=5, max=60, step=1, int=true, value=20 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

local function arr_max(a, from, to) local hi = 0; if from < 1 then from = 1 end; for k = from, to do local v = a[k] or 0; if v > hi then hi = v end end; return hi end
local function arr_min(a, from, to) local lo = math.huge; if from < 1 then from = 1 end; for k = from, to do local v = a[k] or 0; if v > 0 and v < lo then lo = v end end; return (lo == math.huge) and 0 or lo end

function on_bar(ctx)
    local last = ctx.i + 1
    local T = P.T
    if last < T then return "HOLD" end
    -- 唐奇安通道取"今日之前的 N 日"(不含今日), 与 08 放量突破口径一致: 今日收盘突破前 N 日通道
    local up   = arr_max(ctx.high_arr, ctx.i - T + 1, ctx.i)                 -- 近 T 日最高(不含今日)
    local down = arr_min(ctx.low_arr, ctx.i - math.floor(T / 2) + 1, ctx.i)  -- 近 T/2 日最低(不含今日)
    if up <= 0 then return "HOLD" end
    local close = ctx.close_arr[last]
    if not ctx.holding then
        if close > up then return "BUY" end
    else
        if close < down then return "SELL" end
    end
    return "HOLD"
end
