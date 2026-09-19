// main.cpp — 程序入口 (支持 --selftest 自检 / --update-data 数据更新)
#include <QApplication>
#include <QTimer>
#include <cstdio>
#include "mainwindow.h"
#include "netclient.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ---- 诊断模式: stock_gui.exe --diag (打印路径/数据库状态, 排查"文件找不到"类问题) ----
    if (app.arguments().contains(QStringLiteral("--diag"))) {
        printf("=== stock_gui 诊断 (v%s) ===\n", APP_VERSION);
        printf("exe 目录   : %s\n", exeDir().c_str());
        printf("主库路径   : %s\n", resolveDbPath("kline_latest.db").c_str());
        printf("估值库路径 : %s\n", resolveDbPath("valuation.db").c_str());
        Engine eng(resolveDbPath("kline_latest.db"), resolveDbPath("valuation.db"));
        std::string err;
        bool ok = eng.open(err);
        printf("主库打开   : %s%s\n", ok ? "OK" : "FAIL — ", ok ? "" : err.c_str());
        if (ok) {
            printf("最新交易日 : %s\n", eng.latestMarketDate().c_str());
            auto stocks = eng.allStocks();
            printf("股票数量   : %d\n", (int)stocks.size());
        }
        bool hist = eng.ensureHistDb();
        printf("估值库打开 : %s\n", hist ? "OK" : "FAIL");
        if (!hist) printf("            (%s)\n", eng.histOpenErr().c_str());
        if (hist) {
            double pk = eng.pbPercentile3y("600519.SH", 8.0);
            printf("估值查询   : %s\n", pk >= 0 ? "OK" : "可打开但无对应数据");
        }
        printf("=== 诊断结束 ===\n");
        return ok ? 0 : 1;
    }

    // ---- 命令行估值更新: stock_gui.exe --update-valuation [--limit N] ----
    if (app.arguments().contains(QStringLiteral("--update-valuation"))) {
        QStringList args = app.arguments();
        int limit = 0;
        for (int i = 0; i + 1 < args.size(); i++)
            if (args[i] == QStringLiteral("--limit")) limit = args[i+1].toInt();
        QString dbp = QString::fromStdString(resolveDbPath("valuation.db"));
        printf("[val] 估值库: %s\n", dbp.toUtf8().constData());
        QString err;
        bool ok = updateValuationDb(dbp, limit,
            [](int d, int t) { fprintf(stderr, "\r  %d/%d", d, t); fflush(stderr); },
            &err, true);
        printf("\n%s\n", ok ? "[val] OK" : ("[val] FAIL: " + err).toUtf8().constData());
        return ok ? 0 : 1;
    }

    // ---- 命令行数据更新: stock_gui.exe --update-data [--full] [--limit N] ----
    //   默认: 快速更新(批量快照, 秒级, 温柔请求量)
    //   --full: 全量重建(逐只260根历史, 慢速安全模式)
    if (app.arguments().contains(QStringLiteral("--update-data"))) {
        QStringList args = app.arguments();
        bool full = args.contains(QStringLiteral("--full"));
        int limit = 0;
        for (int i = 0; i + 1 < args.size(); i++)
            if (args[i] == QStringLiteral("--limit")) limit = args[i+1].toInt();
        QString dbp = QString::fromStdString(resolveDbPath("kline_latest.db"));
        printf("[update] 数据库: %s\n", dbp.toUtf8().constData());
        QString err;
        if (!full) {
            printf("[update] 快速更新(批量快照)...\n");
            int r = updateIncremental(dbp,
                [](int d, int t) { fprintf(stderr, "\r  %d/%d", d, t); fflush(stderr); },
                &err, true);
            if (r == 1) { printf("\n[update] OK (增量)\n"); return 0; }
            if (r == 2) { printf("\n[update] 已是最新\n"); return 0; }
            printf("\n[update] FAIL: %s\n", err.toUtf8().constData());
            return 1;
        }
        printf("[update] 全量重建(慢速安全模式)...\n");
        bool ok = updateMarketData(dbp, limit,
            [](int d, int t) { fprintf(stderr, "\r  进度 %d/%d", d, t); fflush(stderr); },
            &err, true);
        printf("\n%s\n", ok ? "[update] OK (全量)" : ("[update] FAIL: " + err).toUtf8().constData());
        return ok ? 0 : 1;
    }

    MainWindow w;
    w.showMaximized();   // 启动即最大化(全屏)
    if (app.arguments().contains(QStringLiteral("--selftest"))) {
        w.runSelfTest();
        QTimer::singleShot(300, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
