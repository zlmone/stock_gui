// ============================================================
// main_test.cpp — 引擎命令行测试: engine_test CODE [CODE...]
// 示例: ./engine_test 002730.SZ 603106.SH 000158.SZ
// ============================================================
#include "engine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("用法: engine_test CODE [CODE...]\n");
        printf("  数据库与 exe 同目录即可自动发现, 或设 STOCK_DB_DIR 环境变量\n");
        return 1;
    }
    Engine eng(resolveDbPath("kline_latest.db"), resolveDbPath("valuation.db"));
    std::string err;
    if (!eng.open(err)) { printf("%s\n", err.c_str()); return 1; }
    printf("[db] %s\n", resolveDbPath("kline_latest.db").c_str());
    eng.loadMaps(exeDir() + "/industry_map.csv", exeDir() + "/concept_map.csv");

    printf("%-12s%-10s%8s | %6s%5s%8s%7s | %6s%8s | %7s | %s\n",
           "code","name","price","fixR","n","bt","worst","pos60","vsMA20","PB分位","verdict");
    for (int i = 1; i < argc; i++) {
        std::string code = argv[i];
        std::string name = eng.stockName(code);
        double pb = 0; // 快照注入留给GUI/网络层
        Stats s = eng.analyze(code, name, pb);
        if (!s.ok) { printf("%-12s  -- %s\n", code.c_str(), s.verdict.c_str()); continue; }
        char pbs[16];
        if (s.pbPct3y >= 0) snprintf(pbs, sizeof(pbs), "%.0f%%", s.pbPct3y);
        else snprintf(pbs, sizeof(pbs), "-");
        printf("%-12s%-10s%8.2f | %5.0f%%%5d%+7.1f%%%+6.1f%% | %5.0f%%%+7.1f%% | %7s | %s\n",
               code.c_str(), name.c_str(), s.price,
               s.fixRate, s.events, s.backtest, s.worst,
               s.pos60, s.vsMa20, pbs, s.verdict.c_str());
    }
    return 0;
}
