// pool.cpp — "默认池"持久化实现 (sqlite3)
#include "pool.h"
#include "engine.h"          // resolveDbPath
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>
#include <sqlite3.h>

// 归一化成引擎认识的 "600000.SH" 形式; 解析不出来(不是代码)返回空串。
//   接受: 600519 | 600519.SH | sh600519 | SZ002730 | 002730.sz | bj920002
//   推断规则与 netclient::codeWithSuffix 一致(6/9/5->SH, 4/8/920->BJ, 其余 SZ)
QString poolNormalize(const QString& raw) {
    QString p = raw.trimmed().toUpper();
    if (p.isEmpty()) return QString();

    QString suf;
    const int dot = p.indexOf('.');
    if (dot > 0) {                                   // 已带后缀
        suf = p.mid(dot + 1);
        p = p.left(dot);
    }
    if (p.size() > 2 && !p.at(0).isDigit()
        && (p.startsWith("SH") || p.startsWith("SZ") || p.startsWith("BJ"))) {
        if (suf.isEmpty()) suf = p.left(2);          // 前缀当后缀用(sh600519 -> 600519.SH)
        p = p.mid(2);
    }
    if (p.isEmpty()) return QString();
    bool allDigit = true;
    for (const QChar& ch : p) if (!ch.isDigit()) { allDigit = false; break; }
    if (!allDigit || p.size() < 5 || p.size() > 6) return QString();   // 明显不是股票代码, 丢掉

    if (!suf.isEmpty()) return p + "." + suf;
    if (p.startsWith("920") || p.startsWith('4') || p.startsWith('8')) return p + ".BJ";
    if (p.startsWith('6') || p.startsWith('9') || p.startsWith('5')) return p + ".SH";
    return p + ".SZ";
}

QStringList poolNormalizeList(const QString& raw) {
    QStringList out;
    const QStringList parts = raw.split(QRegularExpression(QStringLiteral("[,\\s;，、]+")), Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        const QString c = poolNormalize(p);
        if (!c.isEmpty() && !out.contains(c)) out << c;
    }
    return out;
}

QString poolDbPath() {
    // 注意: 不能直接用 resolveDbPath —— 它只在"文件已存在"时才认 STOCK_DB_DIR,
    // 而 pool.db 是新建文件, 那样会被建到 exe 目录, 用户设了固定数据目录也留不住池。
    // 所以这里: 设了 STOCK_DB_DIR 就无条件放那儿(目录不存在时由 poolOpen 建), 否则放 exe 同目录。
    const QByteArray env = qgetenv("STOCK_DB_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env) + QStringLiteral("/pool.db");
    return QString::fromStdString(resolveDbPath("pool.db"));
}

// 打开(必要时建库/建目录)。返回 nullptr 时 err 有说明。
static sqlite3* poolOpen(const QString& dbPath, bool readOnly, QString* err) {
    const QFileInfo fi(dbPath);
    if (!readOnly) {
        const QString dir = fi.absolutePath();
        if (!QDir().mkpath(dir)) {
            if (err) *err = QStringLiteral("建不了目录: ") + dir;
            return nullptr;
        }
    } else if (!fi.exists()) {
        if (err) *err = QStringLiteral("池文件不存在");
        return nullptr;
    }
    sqlite3* db = nullptr;
    const int flags = readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db, flags, nullptr) != SQLITE_OK) {
        if (err) *err = QStringLiteral("打不开池文件: ") + dbPath;
        if (db) sqlite3_close(db);
        return nullptr;
    }
    if (!readOnly) {
        char* emsg = nullptr;
        const char* ddl = "CREATE TABLE IF NOT EXISTS pool ("
                          "code TEXT PRIMARY KEY, name TEXT, source TEXT, added_at TEXT)";
        if (sqlite3_exec(db, ddl, nullptr, nullptr, &emsg) != SQLITE_OK) {
            if (err) *err = QStringLiteral("建表失败: ") + (emsg ? emsg : "?");
            if (emsg) sqlite3_free(emsg);
            sqlite3_close(db);
            return nullptr;
        }
    }
    return db;
}

std::vector<PoolItem> poolLoad(const QString& dbPath) {
    std::vector<PoolItem> out;
    QString err;
    sqlite3* db = poolOpen(dbPath, true, &err);
    if (!db) return out;
    sqlite3_stmt* st = nullptr;
    // 按加入顺序(rowid), 不按时间 —— 秒级时间戳会并列, rowid 更稳
    if (sqlite3_prepare_v2(db, "SELECT code,name,source,added_at FROM pool ORDER BY rowid", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            auto txt = [&](int i) -> QString {
                const unsigned char* p = sqlite3_column_text(st, i);
                return p ? QString::fromUtf8((const char*)p) : QString();
            };
            PoolItem it;
            it.code = txt(0); it.name = txt(1); it.source = txt(2); it.addedAt = txt(3);
            if (!it.code.isEmpty()) out.push_back(it);
        }
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

int poolCount(const QString& dbPath) {
    QString err;
    sqlite3* db = poolOpen(dbPath, true, &err);
    if (!db) return -1;
    int n = 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM pool", -1, &st, nullptr) == SQLITE_OK
        && sqlite3_step(st) == SQLITE_ROW) {
        n = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return n;
}

int poolAdd(const QString& dbPath, const std::vector<std::pair<QString, QString>>& codeName,
            const QString& source, QString* err) {
    if (codeName.empty()) return 0;
    sqlite3* db = poolOpen(dbPath, false, err);
    if (!db) return -1;
    const int before = [&]() {
        int n = -1;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM pool", -1, &st, nullptr) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return n;
    }();
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    char* emsg = nullptr;
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_stmt* ins = nullptr;
    // 已存在时: 只补名称/来源, 保留原加入时间(池的顺序才不会因为重新加入而乱跳)
    const char* sql = "INSERT INTO pool(code,name,source,added_at) VALUES(?,?,?,?) "
                      "ON CONFLICT(code) DO UPDATE SET "
                      "name=CASE WHEN excluded.name<>'' THEN excluded.name ELSE pool.name END, "
                      "source=CASE WHEN excluded.source<>'' THEN excluded.source ELSE pool.source END";
    if (sqlite3_prepare_v2(db, sql, -1, &ins, nullptr) != SQLITE_OK) {
        if (err) *err = QStringLiteral("准备写入失败");
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return -1;
    }
    int ok = 0;
    for (const auto& cn : codeName) {
        const QString code = poolNormalize(cn.first);
        if (code.isEmpty()) continue;
        sqlite3_bind_text(ins, 1, code.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 2, cn.second.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, source.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 4, now.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(ins) == SQLITE_DONE) ok++;
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, &emsg) != SQLITE_OK) {
        if (err) *err = QStringLiteral("提交失败: ") + (emsg ? emsg : "?");
        if (emsg) sqlite3_free(emsg);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return -1;
    }
    int after = -1;
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM pool", -1, &st, nullptr) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW) after = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    if (before < 0 || after < 0) return ok;      // 数不清就按写入条数报
    return after - before;
}

int poolRemove(const QString& dbPath, const QStringList& codes, QString* err) {
    if (codes.isEmpty()) return 0;
    sqlite3* db = poolOpen(dbPath, false, err);
    if (!db) return -1;
    sqlite3_stmt* del = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM pool WHERE code=?", -1, &del, nullptr) != SQLITE_OK) {
        if (err) *err = QStringLiteral("准备删除失败");
        sqlite3_close(db);
        return -1;
    }
    int n = 0;
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    for (const QString& raw : codes) {
        const QString code = poolNormalize(raw);
        if (code.isEmpty()) continue;
        sqlite3_bind_text(del, 1, code.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(del) == SQLITE_DONE) n += sqlite3_changes(db);
        sqlite3_reset(del);
    }
    sqlite3_finalize(del);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return n;
}

int poolClear(const QString& dbPath, QString* err) {
    sqlite3* db = poolOpen(dbPath, false, err);
    if (!db) return -1;
    if (sqlite3_exec(db, "DELETE FROM pool", nullptr, nullptr, nullptr) != SQLITE_OK) {
        if (err) *err = QStringLiteral("清空失败");
        sqlite3_close(db);
        return -1;
    }
    const int n = sqlite3_changes(db);
    sqlite3_close(db);
    return n;
}

bool poolContains(const QString& dbPath, const QString& code) {
    const QString want = poolNormalize(code);
    if (want.isEmpty()) return false;
    const auto items = poolLoad(dbPath);
    for (const PoolItem& it : items) if (it.code == want) return true;
    return false;
}

QStringList poolSeedCodes() {
    // 老版本写死在 loadPool() 里的那 19 只 —— 只作为"第一次运行时的初始池",
    // 写进 pool.db 之后就以库为准, 这里的列表不再影响任何行为。
    return QStringList{
        QStringLiteral("002730.SZ"), QStringLiteral("603106.SH"), QStringLiteral("003013.SZ"),
        QStringLiteral("603867.SH"), QStringLiteral("002515.SZ"), QStringLiteral("000006.SZ"),
        QStringLiteral("000572.SZ"), QStringLiteral("002498.SZ"), QStringLiteral("603005.SH"),
        QStringLiteral("000158.SZ"), QStringLiteral("603881.SH"), QStringLiteral("600570.SH"),
        QStringLiteral("300458.SZ"), QStringLiteral("601138.SH"), QStringLiteral("600330.SH"),
        QStringLiteral("002333.SZ"), QStringLiteral("601999.SH"), QStringLiteral("002385.SZ"),
        QStringLiteral("000713.SZ")
    };
}
