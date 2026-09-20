-- MACD 交叉: 由 talib.MACD 的 DIF 与 DEA 比较产生信号, DIF 上穿 DEA 买入, 下穿卖出。
-- 来源: ea-python "MACD指标策略.py"; 策略按 MACD柱(=DIF-DEA 的倍数)符号下单, 等价于 DIF vs DEA。
-- 数组约定: close_arr 1 索引, 当前根 ctx.i 对应 bars[i] => close_arr[ctx.i+1] 为今日收盘。
strategy = { name = "MACD交叉", desc = "DIF上穿DEA买入, 下穿卖出" }

params = {
    { key="fast",    label="快线EMA", min=5,  max=30,  step=1,  int=true,  value=12 },
    { key="slow",    label="慢线EMA", min=10, max=60,  step=1,  int=true,  value=26 },
    { key="signal",  label="信号线EMA", min=2, max=15,  step=1,  int=true,  value=5 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

-- 指数移动平均, seed 取首根收盘; arr 1 索引
local function calcEMA(arr, period)
    local out = {}; local k = 2 / (period + 1)
    out[1] = arr[1]
    for i = 2, #arr do out[i] = arr[i] * k + out[i - 1] * (1 - k) end
    return out
end

function on_bar(ctx)
    local cs = ctx.close_arr                    -- 与引擎一致: 完整历史数组
    local n = ctx.i + 1                          -- 当前根(1 索引), 用 ctx.i 而非 #cs
    if n < P.fast + P.signal + 5 then return "HOLD" end  -- 需足够历史使 EMA 收敛
    local eF = calcEMA(cs, P.fast)
    local eS = calcEMA(cs, P.slow)
    local dif = {}; for i = 1, n do dif[i] = eF[i] - eS[i] end
    local dea = calcEMA(dif, P.signal)
    if dif[n] > dea[n] then return "BUY" end  -- MACD柱>0, 多头
    if dif[n] < dea[n] then return "SELL" end  -- MACD柱<0, 空头
    return "HOLD"
end
