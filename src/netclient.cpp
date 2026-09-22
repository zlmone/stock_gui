// netclient.cpp — 数据更新网络层实现
#include "netclient.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDate>
#include <QTextStream>
#include <QTextCodec>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QSet>
#include <QPointer>
#include <QThread>
#include <cstdio>
#include <sqlite3.h>

// ==================== 工具 ====================
QString toTencentCode(const QString& code) {
    QString pure = code.section('.', 0, 0);
    if (code.endsWith(".SH") || pure.startsWith('6') || pure.startsWith('9') || pure.startsWith('5'))
        return "sh" + pure;
    if (code.endsWith(".BJ"))
        return "bj" + pure;
    return "sz" + pure;
}

static QString fromTencentCode(const QString& tc) {
    if (tc.startsWith("sh")) return tc.mid(2) + ".SH";
    if (tc.startsWith("bj")) return tc.mid(2) + ".BJ";
    return tc.mid(2) + ".SZ";
}

// 腾讯K线URL (前复权, 与现有库口径一致)
static QUrl txUrl(const QString& tc, int n = 260) {
    return QUrl(QStringLiteral("https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=%1,day,,,%2,qfq")
                    .arg(tc).arg(n));
}
// 新浪K线URL (http, 备用通道; 数据为不复权, 注意除权跳变)
static QUrl sinaUrl(const QString& tc, int n = 260) {
    return QUrl(QStringLiteral("http://money.finance.sina.com.cn/quotes_service/api/json_v2.php/"
                               "CN_MarketData.getKLineData?symbol=%1&scale=240&ma=no&datalen=%2")
                    .arg(tc).arg(n));
}

// ==================== 解析 ====================
static QVector<Bar> parseTencent(const QString& tc, const QByteArray& body) {
    QVector<Bar> out;
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonObject data = doc.object().value("data").toObject().value(tc).toObject();
    QJsonArray arr = data.value("qfqday").toArray();
    if (arr.isEmpty()) arr = data.value("day").toArray();
    for (const QJsonValue& v : arr) {
        QJsonArray a = v.toArray();
        if (a.size() < 6) continue;
        Bar b;
        b.date = a[0].toString().toStdString();
        b.open = a[1].toString().toDouble();
        b.close = a[2].toString().toDouble();
        b.high = a[3].toString().toDouble();
        b.low = a[4].toString().toDouble();
        b.volume = a[5].toString().toDouble();   // 单位: 手(与库一致)
        if (b.close > 0 && !b.date.empty()) out.push_back(b);
    }
    return out;
}

static QVector<Bar> parseSina(const QString& tc, const QByteArray& body) {
    QVector<Bar> out;
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonArray arr = doc.array();
    for (const QJsonValue& v : arr) {
        QJsonObject o = v.toObject();
        Bar b;
        b.date = o.value("day").toString().toStdString();
        b.open = o.value("open").toString().toDouble();
        b.high = o.value("high").toString().toDouble();
        b.low = o.value("low").toString().toDouble();
        b.close = o.value("close").toString().toDouble();
        b.volume = o.value("volume").toString().toDouble() / 100.0;   // 新浪:股 -> 手
        if (b.close > 0 && !b.date.empty()) out.push_back(b);
    }
    return out;
}

// ==================== 单块并发拉取 ====================
// 对一批代码并发拉取, 全部完成后返回; 每请求 15s transfer 超时 + 60s 兜底 abort
static int fetchChunk(QNetworkAccessManager& mgr, const QStringList& tcs, bool useTx,
                      QHash<QString, QVector<Bar>>& out, bool verbose) {
    int failCount = 0;
    int pending = tcs.size();
    if (pending == 0) return 0;
    QEventLoop loop;
    QVector<QPointer<QNetworkReply>> reps;
    reps.reserve(tcs.size());

    for (const QString& tc : tcs) {
        QNetworkRequest req(useTx ? txUrl(tc) : sinaUrl(tc));
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
        req.setTransferTimeout(15000);
        if (!useTx) req.setRawHeader("Referer", "http://finance.sina.com.cn");
        QNetworkReply* rep = mgr.get(req);
        reps.push_back(rep);
        QObject::connect(rep, &QNetworkReply::finished, [&out, &failCount, &pending, &loop, rep, tc, useTx, verbose]() {
            QVector<Bar> bars;
            if (rep->error() == QNetworkReply::NoError) {
                QByteArray body = rep->readAll();
                bars = useTx ? parseTencent(tc, body) : parseSina(tc, body);
            } else if (verbose) {
                fprintf(stderr, "  [%s] %s\n", tc.toUtf8().constData(),
                        rep->errorString().toUtf8().constData());
            }
            if (bars.isEmpty()) {
                failCount++;
            } else {
                out.insert(fromTencentCode(tc), bars);
            }
            rep->deleteLater();
            if (--pending == 0) loop.quit();
        });
    }
    // 兜底: 60 秒后 abort 所有未完成 (abort 会触发 finished, pending 归零)
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, [&reps]() {
        for (auto& r : reps)
            if (r && r->isRunning()) r->abort();
    });
    guard.start(60000);
    while (pending > 0) loop.exec();
    return failCount;
}

// ==================== 快照解析 (批量行情) ====================
struct SnapRow {
    QString name;
    double price = 0, prev = 0, open = 0, high = 0, low = 0, volume = 0, amount = 0;
    double turnover = 0;     // 换手率 %
    double mktcap = 0;       // 总市值(亿)
    double fcap = 0;         // 流通市值(亿)
    QString date;   // YYYY-MM-DD (取自时间戳前8位)
    QString time;   // HH:MM:SS (取自时间戳第9-14位, 供实时盯盘判断新鲜度)
};

// stocks 表附加信息 (名称/市值/换手)
struct StockMeta {
    QString name;
    double mktcap = 0, fcap = 0, turnover = 0;
};

static void parseSnapshot(const QByteArray& raw, QHash<QString, SnapRow>& out) {
    QTextCodec* gbk = QTextCodec::codecForName("GBK");
    QString text = gbk ? gbk->toUnicode(raw) : QString::fromLatin1(raw);
    for (const QString& line : text.split(';', Qt::SkipEmptyParts)) {
        int eq = line.indexOf("=\"");
        if (eq < 0) continue;
        QString head = line.left(eq).trimmed();
        if (!head.startsWith("v_")) continue;
        QString tc = head.mid(2);
        QString body = line.mid(eq + 2);
        if (body.endsWith('"')) body.chop(1);
        QStringList p = body.split('~');
        if (p.size() < 38) continue;
        SnapRow r;
        r.name = p[1];
        r.price = p[3].toDouble();
        r.prev = p[4].toDouble();
        r.open = p[5].toDouble();
        r.high = p[33].toDouble();
        r.low = p[34].toDouble();
        r.volume = p[36].toDouble();          // 手
        r.amount = p[37].toDouble() * 1e4;    // 库内单位(亿显示=值/1e6)
        r.turnover = p[38].toDouble();        // 换手率%
        r.fcap = p[44].toDouble();            // 流通市值(亿)
        r.mktcap = p[45].toDouble();          // 总市值(亿)
        if (p[30].length() >= 8) {            // "20260918150000"
            QString d8 = p[30].left(8);
            r.date = d8.left(4) + "-" + d8.mid(4, 2) + "-" + d8.mid(6, 2);
            if (p[30].length() >= 14) {       // 带时分秒 -> 供盯盘显示行情时间
                const QString t6 = p[30].mid(8, 6);
                r.time = t6.left(2) + ":" + t6.mid(2, 2) + ":" + t6.mid(4, 2);
            }
        }
        out.insert(fromTencentCode(tc), r);
    }
}

// ==================== 增量更新 (快照, 温柔模式) ====================
int updateIncremental(const QString& dbPath,
                      const std::function<void(int, int)>& progress,
                      QString* err, bool verbose) {
    // 1. 打开库 + 读状态
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        *err = QStringLiteral("打开数据库失败(文件被占用?)");
        if (db) sqlite3_close(db);
        return 0;
    }
    // 兼容旧库: 补 stocks 表市值/换手列 (已存在则报错, 忽略)
    sqlite3_exec(db, "ALTER TABLE stocks ADD COLUMN mktcap REAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE stocks ADD COLUMN fcap REAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE stocks ADD COLUMN turnover REAL", nullptr, nullptr, nullptr);

    QString lastDate;
    QVector<QString> codes;
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT MAX(date) FROM kline_daily", -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char* d = sqlite3_column_text(st, 0);
                if (d) lastDate = QString::fromUtf8((const char*)d);
            }
            sqlite3_finalize(st);
        }
        if (sqlite3_prepare_v2(db, "SELECT code FROM stocks ORDER BY code", -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char* c = sqlite3_column_text(st, 0);
                if (c) codes.push_back(QString::fromUtf8((const char*)c));
            }
            sqlite3_finalize(st);
        }
    }
    if (codes.isEmpty()) {
        *err = QStringLiteral("库中无股票列表");
        sqlite3_close(db);
        return 0;
    }

    // 2. 批量快照拉取 (400/批, 批间隔 300ms -> 温柔)
    QNetworkAccessManager mgr;
    QHash<QString, SnapRow> snaps;
    const int B = 400;
    int total = codes.size();
    for (int i = 0; i < total; i += B) {
        QStringList tcs;
        for (int j = i; j < i + B && j < total; j++)
            tcs << toTencentCode(codes[j]);
        QNetworkRequest req(QUrl(QStringLiteral("http://qt.gtimg.cn/q=") + tcs.join(',')));
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
        req.setTransferTimeout(12000);
        QNetworkReply* rep = mgr.get(req);
        QEventLoop loop;
        QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (rep->error() == QNetworkReply::NoError)
            parseSnapshot(rep->readAll(), snaps);
        rep->deleteLater();
        if (progress) progress(qMin(i + B, total), total);
        QThread::msleep(300);
    }
    if (snaps.isEmpty()) {
        *err = QStringLiteral("快照接口不可用(检查网络或稍后重试)");
        sqlite3_close(db);
        return 0;
    }

    // 3. 交易日期(众数) + 比较
    QHash<QString, int> dateCount;
    for (auto it = snaps.constBegin(); it != snaps.constEnd(); ++it)
        if (!it.value().date.isEmpty()) dateCount[it.value().date]++;
    QString tradeDate;
    int best = 0;
    for (auto it = dateCount.constBegin(); it != dateCount.constEnd(); ++it)
        if (it.value() > best) { best = it.value(); tradeDate = it.key(); }
    if (tradeDate.isEmpty()) {
        *err = QStringLiteral("快照无有效交易日期");
        sqlite3_close(db);
        return 0;
    }
    if (verbose)
        fprintf(stderr, "[update] 快照交易日: %s | 库内最后: %s | 有效快照 %d 只\n",
                tradeDate.toUtf8().constData(), lastDate.toUtf8().constData(), (int)snaps.size());
    // 是否"已最新"不能只看 MAX(date): 只要库里有一只票是新日期, 整个市场就会被
    // 误判为已最新而直接跳过 —— 结果绝大多数股票停留在旧日期, 界面却提示"已是最新"
    // (用户看到的就是"近10日行情是老的")。这里再看一眼"有多少只票真正拥有该日期",
    // 占比不到一半就仍按未更新处理。
    int stockTotal = 0, stockOnLast = 0;
    if (!lastDate.isEmpty()) {
        sqlite3_stmt* cst = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM stocks", -1, &cst, nullptr) == SQLITE_OK) {
            if (sqlite3_step(cst) == SQLITE_ROW) stockTotal = sqlite3_column_int(cst, 0);
            sqlite3_finalize(cst);
        }
        cst = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(DISTINCT code) FROM kline_daily WHERE date=?",
                               -1, &cst, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(cst, 1, lastDate.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(cst) == SQLITE_ROW) stockOnLast = sqlite3_column_int(cst, 0);
            sqlite3_finalize(cst);
        }
    }
    const bool marketUpToDate = !lastDate.isEmpty() && tradeDate <= lastDate
                             && (stockTotal <= 0 || stockOnLast * 2 >= stockTotal);
    if (verbose)
        fprintf(stderr, "[update] 库内最后日期 %s 上有 %d/%d 只, 判定已最新=%d\n",
                lastDate.toUtf8().constData(), stockOnLast, stockTotal, (int)marketUpToDate);

    if (marketUpToDate) {
        // K线已最新: 仍刷新 stocks 元数据(名称/市值/换手), 供表格展示
        if (verbose) fprintf(stderr, "[update] K线已最新, 刷新股票元数据...\n");
        char* e2 = nullptr;
        sqlite3_exec(db, "BEGIN", nullptr, nullptr, &e2);
        sqlite3_stmt* nm2 = nullptr;
        sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO stocks (code, name, mktcap, fcap, turnover) VALUES (?,?,?,?,?)",
                           -1, &nm2, nullptr);
        int nmCount = 0;
        for (auto it = snaps.constBegin(); it != snaps.constEnd(); ++it) {
            if (it.value().name.isEmpty()) continue;
            sqlite3_bind_text(nm2, 1, it.key().toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(nm2, 2, it.value().name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(nm2, 3, it.value().mktcap);
            sqlite3_bind_double(nm2, 4, it.value().fcap);
            sqlite3_bind_double(nm2, 5, it.value().turnover);
            sqlite3_step(nm2); sqlite3_reset(nm2);
            nmCount++;
        }
        sqlite3_finalize(nm2);
        sqlite3_exec(db, "COMMIT", nullptr, nullptr, &e2);
        if (verbose) fprintf(stderr, "[update] 元数据已刷新: %d 只\n", nmCount);
        sqlite3_close(db);
        return 2;
    }

    // 4. 事务写入 (DELETE+INSERT 保证幂等)
    char* emsg = nullptr;
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, &emsg);
    sqlite3_stmt* del = nullptr, *ins = nullptr, *nm = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM kline_daily WHERE code=? AND date=?", -1, &del, nullptr);
    sqlite3_prepare_v2(db, "INSERT INTO kline_daily VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", -1, &ins, nullptr);
    sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO stocks (code, name, mktcap, fcap, turnover) VALUES (?,?,?,?,?)",
                       -1, &nm, nullptr);
    int n = 0;
    for (auto it = snaps.constBegin(); it != snaps.constEnd(); ++it) {
        const QString& code = it.key();
        const SnapRow& r = it.value();
        if (r.date != tradeDate || r.volume <= 0 || r.price <= 0) continue;   // 停牌/无成交
        double pct = r.prev > 0 ? (r.price - r.prev) / r.prev * 100.0 : 0.0;
        double amp = r.low > 0 ? (r.high - r.low) / r.low * 100.0 : 0.0;
        sqlite3_bind_text(del, 1, code.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(del, 2, tradeDate.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_step(del); sqlite3_reset(del);
        sqlite3_bind_text(ins, 1, code.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 2, tradeDate.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(ins, 3, r.open);
        sqlite3_bind_double(ins, 4, r.high);
        sqlite3_bind_double(ins, 5, r.low);
        sqlite3_bind_double(ins, 6, r.price);
        sqlite3_bind_double(ins, 7, r.volume);
        sqlite3_bind_double(ins, 8, r.amount);
        sqlite3_bind_double(ins, 9, amp);
        sqlite3_bind_double(ins, 10, pct);
        sqlite3_bind_double(ins, 11, r.price - r.prev);
        sqlite3_bind_double(ins, 12, 0.0);
        sqlite3_step(ins); sqlite3_reset(ins);
        if (!r.name.isEmpty()) {
            sqlite3_bind_text(nm, 1, code.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(nm, 2, r.name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(nm, 3, r.mktcap);
            sqlite3_bind_double(nm, 4, r.fcap);
            sqlite3_bind_double(nm, 5, r.turnover);
            sqlite3_step(nm); sqlite3_reset(nm);
        }
        n++;
    }
    sqlite3_finalize(del); sqlite3_finalize(ins); sqlite3_finalize(nm);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, &emsg);
    sqlite3_close(db);
    if (verbose) fprintf(stderr, "[update] 增量完成: %d 只 @ %s\n", n, tradeDate.toUtf8().constData());
    return 1;
}

// ==================== 数据库读写 ====================
static QVector<QString> readCodesFromDb(const QString& dbPath) {
    QVector<QString> out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return out;
    }
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT code, name FROM stocks ORDER BY code", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* c = sqlite3_column_text(st, 0);
            if (c) out.push_back(QString::fromUtf8((const char*)c));
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return out;
}

static QHash<QString, StockMeta> readStockMetaFromDb(const QString& dbPath) {
    QHash<QString, StockMeta> out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return out;
    }
    sqlite3_stmt* st = nullptr;
    // 旧库可能没有 mktcap/fcap/turnover 列: 先试全列, 失败退回 code+name
    if (sqlite3_prepare_v2(db, "SELECT code, name, mktcap, fcap, turnover FROM stocks",
                           -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* c = sqlite3_column_text(st, 0);
            if (!c) continue;
            StockMeta m;
            const unsigned char* n = sqlite3_column_text(st, 1);
            if (n) m.name = QString::fromUtf8((const char*)n);
            m.mktcap = sqlite3_column_double(st, 2);
            m.fcap = sqlite3_column_double(st, 3);
            m.turnover = sqlite3_column_double(st, 4);
            out.insert(QString::fromUtf8((const char*)c), m);
        }
        sqlite3_finalize(st);
    } else if (sqlite3_prepare_v2(db, "SELECT code, name FROM stocks", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* c = sqlite3_column_text(st, 0);
            const unsigned char* n = sqlite3_column_text(st, 1);
            if (c && n) {
                StockMeta m;
                m.name = QString::fromUtf8((const char*)n);
                out.insert(QString::fromUtf8((const char*)c), m);
            }
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return out;
}

// 从旧库读某只股票的K线 (失败继承用)
static QVector<Bar> readBarsFromDb(sqlite3* db, const QString& code) {
    QVector<Bar> out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT date,open,high,low,close,volume FROM kline_daily WHERE code=? ORDER BY date",
                           -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, code.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            Bar b;
            const unsigned char* d = sqlite3_column_text(st, 0);
            b.date = d ? (const char*)d : "";
            b.open = sqlite3_column_double(st, 1);
            b.high = sqlite3_column_double(st, 2);
            b.low = sqlite3_column_double(st, 3);
            b.close = sqlite3_column_double(st, 4);
            b.volume = sqlite3_column_double(st, 5);
            if (!b.date.empty() && b.close > 0) out.push_back(b);
        }
        sqlite3_finalize(st);
    }
    return out;
}

static bool writeDb(const QString& dbPath, const QHash<QString, QVector<Bar>>& data,
                    const QHash<QString, StockMeta>& meta, int expectMin, QString* err) {
    QString tmp = dbPath + ".tmp";
    QFile::remove(tmp);
    sqlite3* db = nullptr;
    if (sqlite3_open(tmp.toUtf8().constData(), &db) != SQLITE_OK) {
        *err = QStringLiteral("创建临时库失败");
        if (db) sqlite3_close(db);
        return false;
    }
    char* emsg = nullptr;
    auto ex = [&](const char* sql) -> bool {
        if (sqlite3_exec(db, sql, nullptr, nullptr, &emsg) != SQLITE_OK) {
            *err = QStringLiteral("SQL错误: ") + (emsg ? emsg : "?");
            if (emsg) sqlite3_free(emsg);
            return false;
        }
        return true;
    };
    if (!ex("CREATE TABLE kline_daily (code TEXT, date TEXT, open REAL, high REAL, low REAL, "
            "close REAL, volume REAL, amount REAL, amplitude REAL, change_pct REAL, "
            "change_amount REAL, turnover_rate REAL)")) { sqlite3_close(db); return false; }
    if (!ex("CREATE TABLE stocks (code TEXT PRIMARY KEY, name TEXT, mktcap REAL, fcap REAL, turnover REAL)")) { sqlite3_close(db); return false; }
    ex("BEGIN");
    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO kline_daily VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", -1, &ins, nullptr);
    long long rows = 0;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const QString& code = it.key();
        const QVector<Bar>& bars = it.value();
        for (int i = 0; i < bars.size(); i++) {
            const Bar& b = bars[i];
            double prev = (i > 0) ? bars[i-1].close : b.open;
            double pct = prev > 0 ? (b.close - prev) / prev * 100.0 : 0.0;
            double amp = b.low > 0 ? (b.high - b.low) / b.low * 100.0 : 0.0;
            double amt = b.volume * 100.0 * b.close;   // 手->股->金额(近似)
            sqlite3_bind_text(ins, 1, code.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 2, b.date.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(ins, 3, b.open);
            sqlite3_bind_double(ins, 4, b.high);
            sqlite3_bind_double(ins, 5, b.low);
            sqlite3_bind_double(ins, 6, b.close);
            sqlite3_bind_double(ins, 7, b.volume);
            sqlite3_bind_double(ins, 8, amt);
            sqlite3_bind_double(ins, 9, amp);
            sqlite3_bind_double(ins, 10, pct);
            sqlite3_bind_double(ins, 11, b.close - b.open);
            sqlite3_bind_double(ins, 12, 0.0);
            sqlite3_step(ins);
            sqlite3_reset(ins);
            rows++;
        }
    }
    sqlite3_finalize(ins);
    ex("COMMIT");
    // 名称 + 市值 + 换手
    sqlite3_stmt* insn = nullptr;
    sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO stocks (code, name, mktcap, fcap, turnover) VALUES (?,?,?,?,?)",
                       -1, &insn, nullptr);
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        StockMeta m = meta.value(it.key());
        QString nm = m.name.isEmpty() ? it.key() : m.name;
        sqlite3_bind_text(insn, 1, it.key().toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insn, 2, nm.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(insn, 3, m.mktcap);
        sqlite3_bind_double(insn, 4, m.fcap);
        sqlite3_bind_double(insn, 5, m.turnover);
        sqlite3_step(insn);
        sqlite3_reset(insn);
    }
    sqlite3_finalize(insn);
    ex("CREATE INDEX idx_kline_code_date ON kline_daily(code, date)");
    sqlite3_close(db);

    if (rows < expectMin) {
        *err = QStringLiteral("数据量不足(%1 行 < %2), 已放弃更新").arg(rows).arg(expectMin);
        QFile::remove(tmp);
        return false;
    }
    // 替换: 旧库 -> .old, 新库 -> 正式, 删 .old
    QString oldp = dbPath + ".old";
    QFile::remove(oldp);
    if (QFile::exists(dbPath) && !QFile::rename(dbPath, oldp)) {
        *err = QStringLiteral("备份旧库失败(文件被占用?)");
        QFile::remove(tmp);
        return false;
    }
    if (!QFile::rename(tmp, dbPath)) {
        QFile::rename(oldp, dbPath);   // 恢复
        *err = QStringLiteral("替换数据库失败");
        QFile::remove(tmp);
        return false;
    }
    QFile::remove(oldp);
    return true;
}

// ==================== 名称+市值+换手刷新 (快照批量, http) ====================
static void refreshStockMeta(QNetworkAccessManager& mgr, const QVector<QString>& codes,
                          QHash<QString, StockMeta>& meta) {
    const int B = 400;
    int nameFails = 0;
    QTextCodec* gbk = QTextCodec::codecForName("GBK");
    for (int i = 0; i < codes.size(); i += B) {
        if (nameFails >= 2) break;   // 快照接口连续失败: 保留旧名称, 不再等待
        QStringList tcs;
        for (int j = i; j < i + B && j < codes.size(); j++)
            tcs << toTencentCode(codes[j]);
        QNetworkRequest req(QUrl(QStringLiteral("http://qt.gtimg.cn/q=") + tcs.join(',')));
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
        req.setTransferTimeout(12000);
        QNetworkReply* rep = mgr.get(req);
        QEventLoop loop;
        QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (rep->error() == QNetworkReply::NoError) {
            QString text = gbk ? gbk->toUnicode(rep->readAll()) : QString::fromLatin1(rep->readAll());
            bool got = false;
            for (const QString& line : text.split(';', Qt::SkipEmptyParts)) {
                int eq = line.indexOf("=\"");
                if (eq < 0) continue;
                QString head = line.left(eq).trimmed();
                if (!head.startsWith("v_")) continue;
                QString tc = head.mid(2);              // sz002385 / sh600519 / bj920002
                QString body = line.mid(eq + 2);
                if (body.endsWith('"')) body.chop(1);
                QStringList p = body.split('~');
                if (p.size() > 45 && !p[1].isEmpty()) {
                    StockMeta m;
                    m.name = p[1];
                    m.turnover = p[38].toDouble();
                    m.fcap = p[44].toDouble();
                    m.mktcap = p[45].toDouble();
                    meta.insert(fromTencentCode(tc), m);
                    got = true;
                }
            }
            nameFails = got ? 0 : (nameFails + 1);
        } else {
            nameFails++;
        }
        rep->deleteLater();
    }
}

// ==================== 实时行情 (批量快照, 供实时盯盘) ====================
QVector<Quote> fetchQuotes(const QStringList& codes) {
    QVector<Quote> out;
    if (codes.isEmpty()) return out;

    QNetworkAccessManager mgr;
    QHash<QString, SnapRow> snaps;
    const int B = 60;                      // 盯盘通常只有几十只, 一批足够
    for (int i = 0; i < codes.size(); i += B) {
        QStringList tcs;
        for (int j = i; j < i + B && j < codes.size(); j++)
            tcs << toTencentCode(codes[j]);
        QNetworkRequest req(QUrl(QStringLiteral("http://qt.gtimg.cn/q=") + tcs.join(',')));
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
        req.setTransferTimeout(8000);
        QNetworkReply* rep = mgr.get(req);
        QEventLoop loop;
        QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (rep->error() == QNetworkReply::NoError)
            parseSnapshot(rep->readAll(), snaps);
        rep->deleteLater();
    }

    for (const QString& c : codes) {
        // parseSnapshot 的键是 fromTencentCode() 的产物(如 "600000.SH"), 带后缀;
        // 而调用方可能传 "600000" 也可能传 "600000.SH", 这里统一成快照表的键再查,
        // 否则格式不一致会全部查不到(表现为"一只都盯不上")。
        QString key = fromTencentCode(toTencentCode(c));
        auto it = snaps.constFind(key);
        if (it == snaps.constEnd()) it = snaps.constFind(c);   // 兜底: 再按原样试一次
        if (it == snaps.constEnd()) continue;
        Quote q;
        q.ok       = true;
        q.code     = c;                                        // 回传给调用方原本的形态
        q.name     = it->name;
        q.price    = it->price;
        q.prev     = it->prev;
        q.open     = it->open;
        q.high     = it->high;
        q.low      = it->low;
        q.volume   = it->volume;
        q.amount   = it->amount;
        q.turnover = it->turnover;
        q.mktcap   = it->mktcap;
        q.date     = it->date;
        q.time     = it->time;
        out.push_back(q);
    }
    return out;
}

// ==================== 主力资金流 (新浪, 单票) ====================
static int g_fundFlowFailures = 0;

bool fundFlowOffline() { return g_fundFlowFailures >= 2; }
void resetFundFlowOffline() { g_fundFlowFailures = 0; }

FundFlow fetchFundFlow(const QString& code, int days) {
    FundFlow ff;
    if (g_fundFlowFailures >= 2) {
        ff.err = QStringLiteral("已跳过(离线模式, 点[更新数据]或重启恢复)");
        return ff;
    }
    QString tc = toTencentCode(code);   // 002730.SZ -> sz002730 (与新浪格式一致)
    QNetworkAccessManager mgr;
    QNetworkRequest req(QUrl(QStringLiteral("http://vip.stock.finance.sina.com.cn/quotes_service/"
        "api/json_v2.php/MoneyFlow.ssl_qsfx_zjlrqs?daima=") + tc));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
    req.setRawHeader("Referer", "http://finance.sina.com.cn");
    req.setTransferTimeout(10000);
    QNetworkReply* rep = mgr.get(req);
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QByteArray body = rep->readAll();
    bool netOk = (rep->error() == QNetworkReply::NoError);
    rep->deleteLater();
    if (!netOk) {
        g_fundFlowFailures++;
        ff.err = QStringLiteral("网络请求失败");
        return ff;
    }

    QJsonArray arr = QJsonDocument::fromJson(body).array();
    if (arr.isEmpty()) { ff.err = QStringLiteral("无资金流数据"); return ff; }
    auto num = [](const QJsonValue& v) -> double {
        if (v.isString()) return v.toString().toDouble();
        if (v.isDouble()) return v.toDouble();
        return 0;
    };
    double net = 0, main = 0;
    int cnt = 0;
    for (const QJsonValue& v : arr) {
        if (cnt >= days) break;
        QJsonObject o = v.toObject();
        net += num(o.value("netamount"));   // 当日总净流入(元)
        main += num(o.value("r0_net"));     // 主力(超大单)净流入(元)
        cnt++;
    }
    if (cnt == 0) { ff.err = QStringLiteral("无有效数据"); return ff; }
    ff.ok = true;
    ff.days = cnt;
    ff.net5 = net / 1e4;                    // 元 -> 万元
    ff.main5 = main / 1e4;
    ff.retail5 = (net - main) / 1e4;
    g_fundFlowFailures = 0;                 // 成功恢复
    return ff;
}

// ==================== 主流程 ====================
bool updateMarketData(const QString& dbPath, int limit,
                      const std::function<void(int, int)>& progress,
                      QString* err, bool verbose) {
    QVector<QString> codes = readCodesFromDb(dbPath);
    if (codes.isEmpty()) {
        *err = QStringLiteral("读取原库代码列表失败(库不存在或不完整)");
        return false;
    }
    if (limit > 0 && codes.size() > limit) codes.resize(limit);
    int total = codes.size();
    if (verbose) fprintf(stderr, "[update] 目标 %d 只 -> %s\n", total, dbPath.toUtf8().constData());

    QNetworkAccessManager mgr;

    // ---- 源探测: 先腾讯(https), 失败降级新浪(http) ----
    bool useTx = true;
    {
        QNetworkRequest req(txUrl("sh600519", 5));
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
        req.setTransferTimeout(10000);
        QNetworkReply* rep = mgr.get(req);
        QEventLoop loop;
        QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        bool ok = (rep->error() == QNetworkReply::NoError &&
                   !parseTencent("sh600519", rep->readAll()).isEmpty());
        rep->deleteLater();
        useTx = ok;
        if (!ok) {
            if (verbose) fprintf(stderr, "[update] 腾讯源不可用, 切换新浪源\n");
            QNetworkRequest r2(sinaUrl("sh600519", 5));
            r2.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
            r2.setRawHeader("Referer", "http://finance.sina.com.cn");
            r2.setTransferTimeout(10000);
            QNetworkReply* p2 = mgr.get(r2);
            QEventLoop l2;
            QObject::connect(p2, &QNetworkReply::finished, &l2, &QEventLoop::quit);
            l2.exec();
            bool ok2 = (p2->error() == QNetworkReply::NoError &&
                        !parseSina("sh600519", p2->readAll()).isEmpty());
            p2->deleteLater();
            if (!ok2) {
                *err = QStringLiteral("行情接口不可用(腾讯/新浪均失败, 检查网络)");
                return false;
            }
        }
    }

    // ---- 并发拉取 (失败收集) ----
    QHash<QString, QVector<Bar>> data;
    data.reserve(total);
    int fails = 0;
    const int B = useTx ? 4 : 3;               // 温柔模式: 低并发
    QElapsedTimer et;
    et.start();
    QStringList failedCodes;
    int consecutiveFullFails = 0;
    for (int i = 0; i < total; i += B) {
        QStringList tcs;
        QVector<QString> chunkCodes;
        for (int j = i; j < i + B && j < total; j++) {
            tcs << toTencentCode(codes[j]);
            chunkCodes.push_back(codes[j]);
        }
        int before = data.size();
        fails += fetchChunk(mgr, tcs, useTx, data, verbose);
        for (const QString& c : chunkCodes)
            if (!data.contains(c)) failedCodes.push_back(c);
        if (data.size() == before) consecutiveFullFails++;
        else consecutiveFullFails = 0;
        // 连续多批颗粒无收 -> 接口疑似限流, 提前止损(剩余全部走继承)
        if (consecutiveFullFails >= 3 && (int)data.size() < total / 2) {
            if (verbose)
                fprintf(stderr, "[update] 连续多批全失败, 接口疑似限流, 提前止损\n");
            for (int j = i + B; j < total; j++) failedCodes.push_back(codes[j]);
            if (progress) progress(total, total);
            break;
        }
        if (!useTx) QThread::msleep(700);      // 新浪源: 批间隔
        else QThread::msleep(250);             // 腾讯源: 批间隔(温柔)
        if (((i / B) % 40) == 39) QThread::msleep(useTx ? 1500 : 4000);   // 周期性长休息
        if (progress) progress(qMin(i + B, total), total);
    }

    // ---- 失败重试 (仅小面积; 大面积=接口限流, 重试无意义) ----
    QStringList stillFailed;
    if (!failedCodes.isEmpty() && (int)failedCodes.size() > total * 3 / 10) {
        if (verbose) fprintf(stderr, "[update] 失败面过大(%d/%d), 跳过重试, 直接继承旧数据\n",
                             (int)failedCodes.size(), total);
        stillFailed = failedCodes;
    } else if (!failedCodes.isEmpty()) {
        if (verbose) fprintf(stderr, "[update] 首轮失败 %d 只, 重试中...\n", (int)failedCodes.size());
        QThread::msleep(600);
        const int B2 = 8;
        for (int i = 0; i < failedCodes.size(); i += B2) {
            QStringList tcs;
            QVector<QString> chunkCodes;
            for (int j = i; j < i + B2 && j < failedCodes.size(); j++) {
                tcs << toTencentCode(failedCodes[j]);
                chunkCodes.push_back(failedCodes[j]);
            }
            fetchChunk(mgr, tcs, useTx, data, verbose);
            for (const QString& c : chunkCodes)
                if (!data.contains(c)) stillFailed.push_back(c);
            QThread::msleep(200);
        }
    }

    // ---- 仍失败: 从旧库继承 (保证不丢股票) ----
    int inherited = 0;
    if (!stillFailed.isEmpty()) {
        sqlite3* olddb = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &olddb, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            for (const QString& c : stillFailed) {
                QVector<Bar> oldBars = readBarsFromDb(olddb, c);
                if (!oldBars.isEmpty()) { data.insert(c, oldBars); inherited++; }
            }
            sqlite3_close(olddb);
        }
    }
    if (verbose)
        fprintf(stderr, "[update] 拉取完成: 在线 %d | 重试后仍失败 %d | 旧库继承 %d | 缺口 %d | 耗时 %.0fs\n",
                (int)(data.size() - inherited), (int)stillFailed.size(), inherited,
                (int)(total - data.size()), et.elapsed() / 1000.0);
    if (data.size() < total / 3) {
        *err = QStringLiteral("失败率过高(成功 %1/%2), 已放弃").arg(data.size()).arg(total);
        return false;
    }

    // ---- 名称/市值/换手: 新库继承旧值 + 快照刷新 ----
    QHash<QString, StockMeta> meta = readStockMetaFromDb(dbPath);
    refreshStockMeta(mgr, codes, meta);

    // ---- 写库 + 替换 ----
    int expectMin = (limit > 0) ? qMax(10, limit * 30) : 1000000;
    if (!writeDb(dbPath, data, meta, expectMin, err))
        return false;
    if (verbose) fprintf(stderr, "[update] 数据库已更新: %s\n", dbPath.toUtf8().constData());
    return true;
}

// ==================== 估值库更新 (东财接口) ====================
// 接口: datacenter-web.eastmoney.com/api/data/v1/get
//   reportName=RPT_VALUEANALYSIS_DET, filter=(SECURITY_CODE="002385")(TRADE_DATE>='2026-05-15')
//   返回: result.data[] = { TRADE_DATE:"2026-09-18 00:00:00", PE_TTM:.., PB_MRQ:.. }
// 估值库 valuation.db 结构: valuation(code, d, pe_ttm, pb)  d 为 'YYYYMMDD' 无横杠

static QString code6of(const QString& code) {
    return code.section('.', 0, 0);
}

// "20260918" -> "2026-09-18"
static QString d8ToDash(const QString& d8) {
    if (d8.size() != 8) return d8;
    return d8.left(4) + "-" + d8.mid(4, 2) + "-" + d8.mid(6, 2);
}

// "2026-09-18 00:00:00" -> "20260918"
static QString dashToD8(const QString& ds) {
    QString d = ds.left(10);
    d.remove('-');
    return d;
}

static QUrl emValUrl(const QString& code6, const QString& dateDash) {
    QUrl u(QStringLiteral("http://datacenter-web.eastmoney.com/api/data/v1/get"));
    QUrlQuery q;
    q.addQueryItem("sortColumns", "TRADE_DATE");
    q.addQueryItem("sortTypes", "1");
    q.addQueryItem("pageSize", "5000");
    q.addQueryItem("pageNumber", "1");
    q.addQueryItem("reportName", "RPT_VALUEANALYSIS_DET");
    q.addQueryItem("columns", "ALL");
    q.addQueryItem("quoteColumns", "");
    q.addQueryItem("source", "WEB");
    q.addQueryItem("client", "WEB");
    QString filter = QString("(SECURITY_CODE=\"%1\")").arg(code6);
    if (!dateDash.isEmpty())
        filter += QString("(TRADE_DATE>='%1')").arg(dateDash);
    q.addQueryItem("filter", filter);
    u.setQuery(q);
    return u;
}

struct ValRow { QString d8; double pe = 0; double pb = 0; };

static QVector<ValRow> parseEmVal(const QByteArray& body) {
    QVector<ValRow> out;
    QJsonDocument doc = QJsonDocument::fromJson(body);
    QJsonArray arr = doc.object().value("result").toObject().value("data").toArray();
    for (const QJsonValue& v : arr) {
        QJsonObject o = v.toObject();
        QString dt = o.value("TRADE_DATE").toString();
        if (dt.size() < 10) continue;
        QString d8 = dashToD8(dt);
        if (d8.size() != 8) continue;
        ValRow r;
        r.d8 = d8;
        QJsonValue pev = o.value("PE_TTM"), pbv = o.value("PB_MRQ");
        r.pe = pev.isNull() ? 0.0 : pev.toDouble();
        if (pbv.isNull()) continue;
        r.pb = pbv.toDouble();
        out.push_back(r);
    }
    return out;
}

bool updateValuationDb(const QString& dbPath, int limit,
                       const std::function<void(int, int)>& progress,
                       QString* err, bool verbose) {
    // 1. 读库中每只股票的最新日期
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        *err = QStringLiteral("打开估值库失败: ") + dbPath;
        if (db) sqlite3_close(db);
        return false;
    }
    QHash<QString, QString> maxD;    // code -> d8
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT code, MAX(d) FROM valuation GROUP BY code", -1, &st, nullptr) != SQLITE_OK) {
            *err = QStringLiteral("估值库结构异常(无 valuation 表?)");
            sqlite3_close(db);
            return false;
        }
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* c = sqlite3_column_text(st, 0);
            const unsigned char* d = sqlite3_column_text(st, 1);
            if (c && d) maxD[QString::fromUtf8((const char*)c)] = QString::fromUtf8((const char*)d);
        }
        sqlite3_finalize(st);
    }
    if (maxD.isEmpty()) {
        *err = QStringLiteral("估值库中无数据");
        sqlite3_close(db);
        return false;
    }
    QVector<QString> codes = maxD.keys().toVector();
    std::sort(codes.begin(), codes.end());
    if (limit > 0 && codes.size() > limit) codes.resize(limit);
    int total = codes.size();
    if (verbose) fprintf(stderr, "[val] 目标 %d 只 -> %s\n", total, dbPath.toUtf8().constData());

    // 2. 并发拉取增量
    QNetworkAccessManager mgr;
    QHash<QString, QVector<ValRow>> got;
    int fails = 0;
    const int B = 24;
    QElapsedTimer et; et.start();
    for (int i = 0; i < total; i += B) {
        int pending = 0;
        QEventLoop loop;
        QVector<QPointer<QNetworkReply>> reps;
        for (int j = i; j < i + B && j < total; j++) {
            const QString& code = codes[j];
            QString dateDash = d8ToDash(maxD.value(code));
            QNetworkRequest req(emValUrl(code6of(code), dateDash));
            req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
            req.setTransferTimeout(15000);
            QNetworkReply* rep = mgr.get(req);
            reps.push_back(rep);
            pending++;
            QObject::connect(rep, &QNetworkReply::finished, [&got, &fails, &pending, &loop, rep, code, verbose]() {
                if (rep->error() == QNetworkReply::NoError) {
                    QVector<ValRow> rows = parseEmVal(rep->readAll());
                    got.insert(code, rows);      // 可能为空(无新数据)
                } else {
                    fails++;
                    if (verbose) fprintf(stderr, "  [%s] %s\n", code.toUtf8().constData(),
                                         rep->errorString().toUtf8().constData());
                }
                rep->deleteLater();
                if (--pending == 0) loop.quit();
            });
        }
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, [&reps]() {
            for (auto& r : reps) if (r && r->isRunning()) r->abort();
        });
        guard.start(60000);
        while (pending > 0) loop.exec();
        QThread::msleep(15);
        if (progress) progress(qMin(i + B, total), total);
    }
    int newRows = 0;
    for (auto it = got.constBegin(); it != got.constEnd(); ++it) newRows += it.value().size();
    if (verbose)
        fprintf(stderr, "[val] 拉取完成: 成功 %d | 失败 %d | 新数据 %d 行 | %.0fs\n",
                (int)got.size(), fails, newRows, et.elapsed() / 1000.0);
    if (got.isEmpty() && fails > 0) {
        *err = QStringLiteral("估值接口不可用(全部失败 %1 只)").arg(fails);
        sqlite3_close(db);
        return false;
    }

    // 3. 写入 (幂等: 先删该股 >= 起点日期的旧行)
    char* emsg = nullptr;
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, &emsg);
    sqlite3_stmt* del = nullptr, *ins = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM valuation WHERE code=? AND d>=?", -1, &del, nullptr);
    sqlite3_prepare_v2(db, "INSERT INTO valuation (code, d, pe_ttm, pb) VALUES (?,?,?,?)", -1, &ins, nullptr);
    int written = 0;
    for (auto it = got.constBegin(); it != got.constEnd(); ++it) {
        const QString& code = it.key();
        const QVector<ValRow>& rows = it.value();
        if (rows.isEmpty()) continue;         // 无新数据: 不动原行
        const QString& cut = maxD.value(code);   // 该股原来的最新日期(那天会被刷新)
        sqlite3_bind_text(del, 1, code.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(del, 2, cut.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_step(del);
        sqlite3_reset(del);
        for (const ValRow& r : rows) {
            sqlite3_bind_text(ins, 1, code.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ins, 2, r.d8.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(ins, 3, r.pe);
            sqlite3_bind_double(ins, 4, r.pb);
            sqlite3_step(ins);
            sqlite3_reset(ins);
            written++;
        }
    }
    sqlite3_finalize(del);
    sqlite3_finalize(ins);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, &emsg);
    sqlite3_close(db);
    if (verbose) fprintf(stderr, "[val] 入库: %d 行\n", written);
    return true;
}

// ==================== 当日全市场资金流 (双数据源) ====================
// 为什么两个源: 东财 clist 一个源被限流时整个功能就废了(实测: 连续翻 60 页后该域名
//   开始"空回复", 换 82./1./7. 等镜像也照样封 —— 封的是 IP, 不是域名),
//   所以再挂一个新浪的排行接口做兜底, 它的请求量只有东财的 1/10。
// 口径差异(界面/脚本要能区分, 所以库里存 src):
//   新浪: 主力净额(r0_net) + 主力净占比(r0_ratio) + 总净流入/成交额/现价/涨跌幅; 无四档明细
//   东财: 主力净额 = 超大单+大单, 另有 超大单/大单/中单/小单 四档
QString codeWithSuffix(const QString& bare6) {
    QString p = bare6.section('.', 0, 0).trimmed();
    if (p.isEmpty()) return p;
    // 920xxx 也是北交所(易与沪市 900xxx B股混淆, 所以先判)
    if (p.startsWith("920") || p.startsWith('4') || p.startsWith('8')) return p + ".BJ";
    if (p.startsWith('6') || p.startsWith('9') || p.startsWith('5')) return p + ".SH";
    return p + ".SZ";
}

static double ffNum(const QJsonObject& o, const char* k) {
    QJsonValue v = o.value(QLatin1String(k));
    if (v.isDouble()) return v.toDouble();
    if (v.isString()) { bool ok = false; double d = v.toString().toDouble(&ok); return ok ? d : 0.0; }
    return 0.0;
}

// 同步发一个请求(项目里其它网络代码都是这个模式)
static bool ffGet(QNetworkAccessManager& mgr, const QString& url, QByteArray* body, QString* why) {
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0"));
    req.setRawHeader("Referer", "http://finance.sina.com.cn");   // 新浪/东财都认这个, 不带偶发 403
    req.setTransferTimeout(15000);
    QNetworkReply* rep = mgr.get(req);
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    *body = rep->readAll();
    const bool netOk = (rep->error() == QNetworkReply::NoError);
    const int http = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!netOk) {
        *why = QStringLiteral("网络错误(%1)").arg(rep->errorString());
    } else if (http >= 400) {
        *why = QStringLiteral("HTTP %1").arg(http);
    }
    rep->deleteLater();
    return netOk && http < 400;
}

// ---------- 新浪: 个股资金流排行(全市场, num=1000/次) ----------
// 返回 0=失败 1=成功; out 按代码去重
static int ffFetchSina(QHash<QString, FundFlowDay>& out, QString& why, bool verbose,
                       const std::function<void(int, int)>& progress) {
    QNetworkAccessManager mgr;
    const int NUM = 1000;
    int page = 0;
    for (page = 1; page <= 20; page++) {
        QString url = QStringLiteral("http://vip.stock.finance.sina.com.cn/quotes_service/api/"
                                     "json_v2.php/MoneyFlow.ssl_bkzj_ssggzj"
                                     "?page=%1&num=%2&sort=r0_net&asc=0&bankuai=&shangzhang=")
                          .arg(page).arg(NUM);
        QByteArray body;
        QString err;
        bool ok = false;
        for (int attempt = 0; attempt < 3 && !ok; attempt++) {
            if (attempt) QThread::msleep(700 * attempt);
            ok = ffGet(mgr, url, &body, &err);
        }
        if (!ok) {
            why = QStringLiteral("新浪第 %1 页: %2").arg(page).arg(err);
            if (page == 1) return 0;          // 第一页就挂 -> 这个源不可用
            break;                            // 后面挂 -> 已经拿到一部分, 收工
        }
        QJsonArray arr = QJsonDocument::fromJson(body).array();
        if (arr.isEmpty()) {
            if (page == 1) { why = QStringLiteral("新浪返回空数据"); return 0; }
            break;
        }
        for (const QJsonValue& v : arr) {
            QJsonObject o = v.toObject();
            const QString sym = o.value("symbol").toString();      // sh603893 / sz002354 / bj430047
            if (sym.size() < 3) continue;
            const QString suf = sym.left(2).toUpper();
            FundFlowDay f;
            f.code = (sym.mid(2) + "." + suf).toStdString();
            f.name = o.value("name").toString().toStdString();
            f.close = ffNum(o, "trade");
            f.changePct = ffNum(o, "changeratio") * 100.0;         // 新浪给的是小数
            f.mainNet = ffNum(o, "r0_net");                        // 主力净额(元)
            f.mainPct = ffNum(o, "r0_ratio") * 100.0;              // 主力净占比(小数->%)
            f.superNet = f.bigNet = f.midNet = f.smallNet = 0;     // 新浪没有四档
            f.src = "sina";
            out.insert(QString::fromStdString(f.code), f);
        }
        if (progress) progress(qMin(out.size(), 6000), 6000);
        if (arr.size() < NUM) break;          // 最后一页
        QThread::msleep(220);
    }
    if (verbose) fprintf(stderr, "[ff] 新浪: %d 页 -> %d 只\n", page, out.size());
    return out.isEmpty() ? 0 : 1;
}

// ---------- 东财: 资金流排行(四档明细; 页大容易被限流) ----------
static const char* const kFfHosts[] = {
    "82.push2.eastmoney.com", "1.push2.eastmoney.com", "push2.eastmoney.com"
};
static QString ffEmUrl(const char* host, int pn, int pz) {
    return QStringLiteral("http://%1/api/qt/clist/get?pn=%2&pz=%3&po=1&np=1"
                          "&fltt=2&invt=2&fid=f62"
                          "&fs=m:0+t:6,m:0+t:80,m:1+t:2,m:1+t:23,m:0+t:81+s:2048"
                          "&fields=f12,f14,f2,f3,f62,f184,f66,f72,f78,f84,f124")
        .arg(QLatin1String(host)).arg(pn).arg(pz);
}

static int ffFetchEm(QHash<QString, FundFlowDay>& out, QString& dateStr, int& total, QString& why,
                     bool verbose, const std::function<void(int, int)>& progress) {
    QNetworkAccessManager mgr;
    QDateTime maxTs;
    // 先探一次"每页最大给多少": 要 1000, 服务器多半只给 100 —— 拿到真实页大小再正式翻页,
    // 免得中途改 pz 导致 offset 错位漏数据。
    int pz = 1000;
    {
        QByteArray body; QString err;
        bool ok = false;
        for (int attempt = 0; attempt < 2 && !ok; attempt++) {
            if (attempt) QThread::msleep(800);
            ok = ffGet(mgr, ffEmUrl(kFfHosts[attempt % 3], 1, pz), &body, &err);
        }
        if (!ok) { why = QStringLiteral("东财第 1 页: %1").arg(err); return 0; }
        QJsonObject data = QJsonDocument::fromJson(body).object().value("data").toObject();
        QJsonArray diff = data.value("diff").toArray();
        if (diff.isEmpty()) {
            why = QStringLiteral("东财返回空数据(常见原因: 本机 IP 被限流, 过 10-30 分钟再试)");
            return 0;
        }
        total = data.value("total").toInt();
        if (diff.size() < pz) pz = diff.size();      // 服务器实际页大小
        if (pz < 20) pz = 20;
        // 探到的这一页只用来量"服务器每页给多少", 正式翻页从 pn=1 重来(避免 offset 错位)
    }
    const int pages = (total > 0) ? (total + pz - 1) / pz : 200;
    int failedPages = 0;
    for (int pn = 1; pn <= pages && pn <= 200; pn++) {
        QJsonArray diff;
        QString lastErr = QStringLiteral("无响应");
        for (int attempt = 0; attempt < 3; attempt++) {
            const char* host = kFfHosts[attempt % 3];
            QByteArray body; QString err;
            if (!ffGet(mgr, ffEmUrl(host, pn, pz), &body, &err)) {
                lastErr = err;
                QThread::msleep(700 * (attempt + 1));
                continue;
            }
            QJsonObject data = QJsonDocument::fromJson(body).object().value("data").toObject();
            diff = data.value("diff").toArray();
            if (!diff.isEmpty()) break;
            lastErr = QStringLiteral("空回复(多半被限流)");
            QThread::msleep(700 * (attempt + 1));
        }
        if (diff.isEmpty()) {
            failedPages++;
            why = QStringLiteral("第 %1/%2 页失败(%3)").arg(pn).arg(pages).arg(lastErr);
            if (pn == 1) return 0;                       // 第一页就挂 -> 换源
            break;                                       // 中途挂 -> 用已拿到的部分
        }
        for (const QJsonValue& v : diff) {
            QJsonObject o = v.toObject();
            const QString bare = o.value("f12").toString();
            if (bare.isEmpty()) continue;
            FundFlowDay f;
            f.code = codeWithSuffix(bare).toStdString();
            f.name = o.value("f14").toString().toStdString();
            f.close = ffNum(o, "f2");
            f.changePct = ffNum(o, "f3");
            f.mainNet = ffNum(o, "f62");
            f.mainPct = ffNum(o, "f184");
            f.superNet = ffNum(o, "f66");
            f.bigNet = ffNum(o, "f72");
            f.midNet = ffNum(o, "f78");
            f.smallNet = ffNum(o, "f84");
            f.src = "em";
            const double ts = ffNum(o, "f124");
            if (ts > 1e9) {
                QDateTime dt = QDateTime::fromSecsSinceEpoch((qint64)ts);
                if (!maxTs.isValid() || dt > maxTs) maxTs = dt;
            }
            out.insert(QString::fromStdString(f.code), f);       // 按代码去重(翻页期间排序会变)
        }
        if (progress) progress(pn, pages);
        if (total > 0 && out.size() >= total) break;
        if (diff.size() < pz) break;
        QThread::msleep(250);
    }
    if (maxTs.isValid()) dateStr = maxTs.toString(QStringLiteral("yyyy-MM-dd"));
    if (verbose) fprintf(stderr, "[ff] 东财: %d 只 / 接口 total=%d / 失败页=%d\n",
                         out.size(), total, failedPages);
    return out.isEmpty() ? 0 : 1;
}

// ---------- 落库 ----------
static bool ffSave(const QString& dbPath, const QHash<QString, FundFlowDay>& rows, const QString& date,
                   QString* err) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        *err = QStringLiteral("打不开资金流库: ") + dbPath;
        if (db) sqlite3_close(db);
        return false;
    }
    char* emsg = nullptr;
    auto ex = [&](const char* sql) -> bool {
        if (sqlite3_exec(db, sql, nullptr, nullptr, &emsg) != SQLITE_OK) {
            *err = QStringLiteral("SQL错误: ") + (emsg ? emsg : "?");
            if (emsg) sqlite3_free(emsg);
            return false;
        }
        return true;
    };
    if (!ex("CREATE TABLE IF NOT EXISTS fundflow_daily (code TEXT, date TEXT, name TEXT, close REAL,"
            " change_pct REAL, main_net REAL, main_pct REAL, super_net REAL, big_net REAL,"
            " mid_net REAL, small_net REAL, src TEXT, PRIMARY KEY(code, date))")) {
        sqlite3_close(db);
        return false;
    }
    ex("CREATE INDEX IF NOT EXISTS idx_ff_date ON fundflow_daily(date)");
    ex("ALTER TABLE fundflow_daily ADD COLUMN src TEXT");    // 老库补列(已存在会报错, 忽略)

    if (!ex("BEGIN")) { sqlite3_close(db); return false; }
    sqlite3_stmt* ins = nullptr;
    const char* sql = "INSERT OR REPLACE INTO fundflow_daily"
                      "(code,date,name,close,change_pct,main_net,main_pct,super_net,big_net,mid_net,small_net,src)"
                      " VALUES (?,?,?,?,?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &ins, nullptr) != SQLITE_OK) {
        *err = QStringLiteral("准备写入失败: ") + sqlite3_errmsg(db);
        ex("ROLLBACK");
        sqlite3_close(db);
        return false;
    }
    for (auto it = rows.constBegin(); it != rows.constEnd(); ++it) {
        const FundFlowDay& f = it.value();
        sqlite3_bind_text(ins, 1, f.code.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 2, date.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, f.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(ins, 4, f.close);
        sqlite3_bind_double(ins, 5, f.changePct);
        sqlite3_bind_double(ins, 6, f.mainNet);
        sqlite3_bind_double(ins, 7, f.mainPct);
        sqlite3_bind_double(ins, 8, f.superNet);
        sqlite3_bind_double(ins, 9, f.bigNet);
        sqlite3_bind_double(ins, 10, f.midNet);
        sqlite3_bind_double(ins, 11, f.smallNet);
        sqlite3_bind_text(ins, 12, f.src.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, &emsg) != SQLITE_OK) {
        *err = QStringLiteral("提交失败: ") + (emsg ? emsg : "?");
        if (emsg) sqlite3_free(emsg);
        ex("ROLLBACK");
        sqlite3_close(db);
        return false;
    }
    sqlite3_close(db);
    return true;
}

// 把每次更新的结果追加到 fundflow_log.txt (出问题时用户直接把文件发来就行)
static void ffLog(const QString& dbPath, const QString& line) {
    const QFileInfo fi(dbPath);
    QFile f(fi.absolutePath() + QStringLiteral("/fundflow_log.txt"));
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    ts << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) << "  " << line << "\n";
}

int updateFundFlowToday(const QString& dbPath, int srcPref, bool force,
                        const std::function<void(int, int)>& progress,
                        QString* err, QString* outInfo, bool verbose) {
    err->clear();
    if (outInfo) outInfo->clear();

    // 已抓到当天数据就别重复打接口(force 例外)
    if (!force) {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
            sqlite3_stmt* st = nullptr;
            int cnt = 0;
            if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM fundflow_daily WHERE date>=?", -1, &st, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(st, 1, today.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(st) == SQLITE_ROW) cnt = sqlite3_column_int(st, 0);
            }
            sqlite3_finalize(st);
            sqlite3_close(db);
            if (cnt >= 1000) {          // 覆盖上千只 -> 视为已有当天数据
                if (outInfo) *outInfo = QStringLiteral("已有当天数据 %1 只, 跳过抓取").arg(cnt);
                return 2;
            }
        }
    }

    QHash<QString, FundFlowDay> rows;
    QString why, date, srcName;
    int total = 0;
    QElapsedTimer et; et.start();

    const bool trySinaFirst = (srcPref == 0 || srcPref == 1);
    if (trySinaFirst) {
        if (ffFetchSina(rows, why, verbose, progress) == 1) {
            srcName = QStringLiteral("新浪");
            // 新浪不返回行情日期: 用本机日期(周末回退到周五) —— 收盘后跑就是当天
            QDate d = QDate::currentDate();
            if (d.dayOfWeek() == 6) d = d.addDays(-1);
            else if (d.dayOfWeek() == 7) d = d.addDays(-2);
            date = d.toString(QStringLiteral("yyyy-MM-dd"));
        } else if (srcPref == 1) {
            *err = QStringLiteral("新浪接口不可用: %1").arg(why);
            ffLog(dbPath, QStringLiteral("FAIL sina: %1").arg(why));
            return 0;
        }
    }
    if (rows.isEmpty() && (srcPref == 0 || srcPref == 2)) {
        const QString w1 = why;
        if (ffFetchEm(rows, date, total, why, verbose, progress) == 1) {
            srcName = QStringLiteral("东财");
        } else {
            *err = srcPref == 2 ? QStringLiteral("东财接口不可用: %1").arg(why)
                                : QStringLiteral("两个数据源都失败了 —— 新浪: %1; 东财: %2").arg(w1, why);
            ffLog(dbPath, QStringLiteral("FAIL em: %1").arg(why));
            return 0;
        }
    }
    if (rows.isEmpty()) {
        *err = QStringLiteral("未取到任何资金流数据");
        ffLog(dbPath, QStringLiteral("FAIL empty: %1").arg(why));
        return 0;
    }
    if (date.isEmpty()) date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    QString serr;
    if (!ffSave(dbPath, rows, date, &serr)) {
        *err = serr;
        ffLog(dbPath, QStringLiteral("FAIL save: %1").arg(serr));
        return 0;
    }
    int inflow = 0;
    for (auto it = rows.constBegin(); it != rows.constEnd(); ++it) if (it.value().mainNet > 0) inflow++;
    const QString cov = (total > 0) ? QStringLiteral(" 覆盖 %1/%2").arg(rows.size()).arg(total) : QString();
    if (outInfo) {
        *outInfo = QStringLiteral("数据源 %1 | 日期 %2 | %3 只(净流入 %4)%5 | 耗时 %6 秒")
                       .arg(srcName).arg(date).arg(rows.size()).arg(inflow).arg(cov)
                       .arg(et.elapsed() / 1000.0, 0, 'f', 1);
    }
    ffLog(dbPath, QStringLiteral("OK src=%1 date=%2 rows=%3 total=%4 inflow=%5 %.1fs %6")
                      .arg(srcName).arg(date).arg(rows.size()).arg(total).arg(inflow)
                      .arg(et.elapsed() / 1000.0, 0, 'f', 1).arg(why));
    if (verbose) fprintf(stderr, "[ff] %s\n", outInfo ? outInfo->toUtf8().constData() : "");
    return 1;
}

std::vector<FundFlowDay> loadFundFlow(const QString& dbPath, const QString& date,
                                     int limit, QString* outDate) {
    std::vector<FundFlowDay> out;
    if (outDate) outDate->clear();
    if (!QFile::exists(dbPath)) return out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return out;
    }
    QString d = date;
    if (d.isEmpty()) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT MAX(date) FROM fundflow_daily", -1, &st, nullptr) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* p = sqlite3_column_text(st, 0);
            if (p) d = QString::fromUtf8((const char*)p);
        }
        sqlite3_finalize(st);
    }
    if (outDate) *outDate = d;
    if (!d.isEmpty()) {
        // src 是后加的列: 老库没有, 先试带 src 的查询, 失败再退回不带 src 的
        const QString cols = QStringLiteral("code,date,name,close,change_pct,main_net,main_pct,"
                                            "super_net,big_net,mid_net,small_net");
        QString sql = QStringLiteral("SELECT %1,src FROM fundflow_daily WHERE date=? ORDER BY main_net DESC").arg(cols);
        if (limit > 0) sql += QStringLiteral(" LIMIT %1").arg(limit);
        sqlite3_stmt* st = nullptr;
        bool withSrc = (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &st, nullptr) == SQLITE_OK);
        if (!withSrc) {
            sql = QStringLiteral("SELECT %1 FROM fundflow_daily WHERE date=? ORDER BY main_net DESC").arg(cols);
            if (limit > 0) sql += QStringLiteral(" LIMIT %1").arg(limit);
            withSrc = false;
            if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &st, nullptr) != SQLITE_OK) st = nullptr;
        }
        if (st) {
            sqlite3_bind_text(st, 1, d.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(st) == SQLITE_ROW) {
                auto txt = [&](int i) -> QString {
                    const unsigned char* p = sqlite3_column_text(st, i);
                    return p ? QString::fromUtf8((const char*)p) : QString();
                };
                FundFlowDay f;
                f.code = txt(0).toStdString(); f.date = txt(1).toStdString(); f.name = txt(2).toStdString();
                f.close = sqlite3_column_double(st, 3);
                f.changePct = sqlite3_column_double(st, 4);
                f.mainNet = sqlite3_column_double(st, 5);
                f.mainPct = sqlite3_column_double(st, 6);
                f.superNet = sqlite3_column_double(st, 7);
                f.bigNet = sqlite3_column_double(st, 8);
                f.midNet = sqlite3_column_double(st, 9);
                f.smallNet = sqlite3_column_double(st, 10);
                f.src = withSrc ? txt(11).toStdString() : std::string();
                out.push_back(f);
            }
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return out;
}

FundFlowDay fundFlowOf(const QString& dbPath, const QString& code) {
    FundFlowDay f;
    if (!QFile::exists(dbPath) || code.isEmpty()) return f;
    QString keyQ = code.contains('.') ? code : codeWithSuffix(code);
    std::string key = keyQ.toStdString();
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return f;
    }
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT code,date,name,close,change_pct,main_net,main_pct,super_net,"
                               "big_net,mid_net,small_net,src FROM fundflow_daily WHERE code=? "
                               "ORDER BY date DESC LIMIT 1", -1, &st, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return f;
    }
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        auto txt = [&](int i) -> QString {
            const unsigned char* p = sqlite3_column_text(st, i);
            return p ? QString::fromUtf8((const char*)p) : QString();
        };
        f.code = txt(0).toStdString(); f.date = txt(1).toStdString(); f.name = txt(2).toStdString();
        f.close = sqlite3_column_double(st, 3);
        f.changePct = sqlite3_column_double(st, 4);
        f.mainNet = sqlite3_column_double(st, 5);
        f.mainPct = sqlite3_column_double(st, 6);
        f.superNet = sqlite3_column_double(st, 7);
        f.bigNet = sqlite3_column_double(st, 8);
        f.midNet = sqlite3_column_double(st, 9);
        f.smallNet = sqlite3_column_double(st, 10);
        f.src = txt(11).toStdString();
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return f;
}
