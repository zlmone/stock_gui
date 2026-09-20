-- 网格交易 (全仓近似版)。
-- 来源: ea-python 网格交易策略.py。原始为多档仓位/现金分配模型, 本平台为全仓单票 BUY/SELL
-- 模型, 故按"相对基准价的价格分档方向"近似: 基准价=首次跌破 0.85*MA20 时的价, 低价区买入、高价区卖出。
strategy = { name = "网格交易(近似)", desc = "相对基准价低买高卖, 含止盈" }
params = {
    { key="sma_window", label="基准均线(日)", min=5, max=60,  step=1,  int=true,  value=20 },
    { key="price_thr",  label="建仓阈值", min=0.5, max=1.0,  step=0.05,  value=0.85 },
    { key="stop_win",   label="止盈倍数", min=1.0, max=10,  step=0.5,  value=5.0 },
    { key="buy_zone",   label="买入比例阈值", min=0.5, max=1.0,  step=0.01,  value=0.97 },
    { key="sell_zone",  label="卖出比例阈值", min=1.0, max=2.0,  step=0.01,  value=1.10 },
}
P = {}
for _, v in ipairs(params) do P[v.key] = v.value end
local function arr_mean(a,f,t) local s=0 for i=f,t do s=s+a[i] end return s/(t-f+1) end
local base_price  -- 基准价(文件级, 确定后保持不变)
local stop_trig   -- 止盈已触发
function on_bar(ctx)
    local last = ctx.i + 1
    if stop_trig then return "HOLD" end
    if ctx.equity >= P.stop_win then stop_trig = true; if ctx.holding then return "SELL" end; return "HOLD" end
    if base_price == nil then
        if last >= P.sma_window then
            local s = arr_mean(ctx.close_arr, last-P.sma_window+1, last)
            if ctx.close_arr[last] < P.price_thr * s then base_price = ctx.close_arr[last] end
        end
    end
    if base_price == nil then return "HOLD" end
    local ratio = ctx.close_arr[last] / base_price
    if not ctx.holding then
        if ratio < P.buy_zone then return "BUY" end
        return "HOLD"
    else
        if ratio > P.sell_zone then return "SELL" end
        return "HOLD"
    end
end
