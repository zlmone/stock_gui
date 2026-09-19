// bt_test.cpp — Lua 策略回测命令行工具
// 用法: bt_test <策略.lua> <代码> [代码...]
//   bt_test 急杀反弹.lua 002385.SZ 002730.SZ
//   bt_test --list                       列出可用策略
#include "engine.h"
#include "backtest.h"
#include <cstdio>
#include <cstring>
#include <ctime>

int main(int argc, char** argv) {
    std::string sdir = resolveStrategiesDir();
    Backtest bt(sdir);

    if (argc >= 2 && strcmp(argv[1], "--list") == 0) {
        printf("策略目录: %s\n", sdir.c_str());
        for (auto& f : bt.strategyFiles())
            printf("  %-24s %s\n", f.c_str(), bt.displayName(f).c_str());
        return 0;
    }
    if (argc < 3) {
        printf("用法: bt_test <策略.lua> <代码> [代码...]\n");
        printf("     : bt_test --list\n");
        printf("     : bt_test --scan <策略.lua> [数量限制]   # 全市场形态筛选\n");
        return 1;
    }

    // ---- 全市场形态筛选模式 ----
    if (strcmp(argv[1], "--scan") == 0) {
        std::string luaPath = sdir + "/" + argv[2];
        ScanSession sess(luaPath);
        if (!sess.ok()) { printf("策略加载失败: %s\n", sess.error().c_str()); return 1; }
        printf("形态筛选策略: %s\n", sess.strategyName.c_str());

        Engine eng2(resolveDbPath("kline_latest.db"), resolveDbPath("valuation.db"));
        std::string err2;
        if (!eng2.open(err2)) { printf("%s\n", err2.c_str()); return 1; }

        auto stocks = eng2.allStocks();
        int limit = (argc >= 4) ? atoi(argv[3]) : 0;
        if (limit > 0 && (int)stocks.size() > limit) stocks.resize(limit);
        int n = (int)stocks.size();
        printf("扫描 %d 只...\n\n", n);

        clock_t t0 = clock();
        int hits = 0, fails = 0;
        for (int i = 0; i < n; i++) {
            auto bars = eng2.loadSeries(stocks[i].first, 600);
            auto r = sess.test(bars);
            if (!r.ok) { fails++; continue; }
            if (r.passed) {
                hits++;
                printf("  %-11s %-8s %7.2f | %s\n",
                       stocks[i].first.c_str(), stocks[i].second.c_str(),
                       bars.empty() ? 0.0 : bars.back().close, r.detail.c_str());
            }
            if ((i & 255) == 255) { printf("  ... %d/%d 命中%d\r", i + 1, n, hits); fflush(stdout); }
        }
        double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("\n=== 完成: 命中 %d / %d 只 (错误%d) | 耗时 %.1f 秒 ===\n", hits, n, fails, sec);
        return 0;
    }

    Engine eng(resolveDbPath("kline_latest.db"), resolveDbPath("valuation.db"));
    std::string err;
    if (!eng.open(err)) { printf("%s\n", err.c_str()); return 1; }

    std::string luaFile = argv[1];
    printf("策略: %s | 目录: %s\n\n", bt.displayName(luaFile).c_str(), sdir.c_str());

    for (int i = 2; i < argc; i++) {
        std::string code = argv[i];
        auto bars = eng.loadSeries(code, 600);
        std::string name = eng.stockName(code);
        if (bars.size() < 30) { printf("%-12s %s: K线不足\n", code.c_str(), name.c_str()); continue; }

        BacktestResult r = bt.run(bars, luaFile);
        if (!r.ok) { printf("%-12s %s: %s\n", code.c_str(), name.c_str(), r.error.c_str()); continue; }

        printf("%-11s %-8s | %d笔 胜率%.0f%% 收益%+.1f%% 最差%+.1f%% 均持%.1f日 回撤%.1f%%\n",
               code.c_str(), name.c_str(), r.n_trades, r.win_rate, r.total_return,
               r.worst_trade, r.avg_days, r.max_drawdown);
        // 最近3笔明细
        int show = (int)r.trades.size() < 3 ? (int)r.trades.size() : 3;
        for (int k = (int)r.trades.size() - show; k < (int)r.trades.size(); k++) {
            const Trade& t = r.trades[k];
            printf("    %s 买%.2f → %s 卖%.2f | %+.1f%% %d日 (%s)\n",
                   t.buy_date.c_str(), t.buy_price, t.sell_date.c_str(), t.sell_price,
                   t.gain_pct, t.days, t.reason.c_str());
        }
    }
    return 0;
}
