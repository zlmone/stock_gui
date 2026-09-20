-- 均线多头追涨: MA5>MA10>MA20 多头排列 + 收盘站上 MA20 买入, 跌破 MA10 / 持满到期 / 小止损离场
-- 定位: 趋势跟随(顺势而为), 与 02 双均线金叉的区别 —— 02 死叉即走, 本策略持有整段多头趋势,
--       以 MA10 为移动止盈线, 只在趋势走坏时才离场。
strategy = { name = "均线多头追涨", desc = "多头排列+站上MA20买入, 跌破MA10离场" }

params = {
    { key="hold",  label="最长持有日", min=1,  max=60,  step=1,  int=true,  value=15 },
    { key="stop",  label="固定止损%",  min=-20, max=-2, step=0.5, value=-5 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end

function on_bar(ctx)
    if ctx.ma5 == nil or ctx.ma10 == nil or ctx.ma20 == nil then return "HOLD" end
    if not ctx.holding then
        if ctx.ma5 > ctx.ma10 and ctx.ma10 > ctx.ma20 and ctx.close > ctx.ma20 then
            return "BUY"
        end
    else
        if ctx.days >= P.hold then return "SELL" end
        if ctx.ma5 < ctx.ma10 then return "SELL" end   -- 趋势走弱
        if ctx.gain < P.stop / 100 then return "SELL" end  -- 触达固定止损
    end
    return "HOLD"
end
