// ============================================================
// engine.cpp — 四维分析引擎实现
// ============================================================
#include "engine.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>

std::string normDate(const std::string& d) {
    std::string s; s.reserve(d.size());
    for (char c : d) if (c != '-') s += c;
    return s;
}

std::string Engine::latestMarketDate() {
    if (!latestDateCache_.empty()) return latestDateCache_;   // 缓存命中
    if (!dbLatest_) return "";
    sqlite3_stmt* st = nullptr;
    std::string out;
    if (sqlite3_prepare_v2(dbLatest_, "SELECT MAX(date) FROM kline_daily", -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* d = sqlite3_column_text(st, 0);
            if (d) out = (const char*)d;
        }
        sqlite3_finalize(st);
    }
    latestDateCache_ = out;
    return out;
}

void Engine::close() {
    if (dbLatest_) { sqlite3_close(dbLatest_); dbLatest_ = nullptr; }
    if (dbHist_) { sqlite3_close(dbHist_); dbHist_ = nullptr; }
    latestDateCache_.clear();
}

// 日期差(天): b - a, 输入 yyyy-mm-dd; 解析失败返回 0
static int dateDiffDays(const std::string& a, const std::string& b) {
    auto toJDN = [](const std::string& s) -> long long {
        int y = 0, m = 0, d = 0;
        if (s.size() < 10) return -1;
        y = atoi(s.substr(0, 4).c_str());
        m = atoi(s.substr(5, 2).c_str());
        d = atoi(s.substr(8, 2).c_str());
        if (y <= 0 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
        if (m <= 2) { y--; m += 12; }
        return 365LL*y + y/4 - y/100 + y/400 + (153LL*(m-3)+2)/5 + d;
    };
    long long ja = toJDN(a), jb = toJDN(b);
    if (ja < 0 || jb < 0) return 0;
    return (int)(jb - ja);
}

// ---------- 路径工具 ----------
std::string exeDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return ".";
    std::string s(buf, n);
#else
    char buf[4096] = {0};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    std::string s(buf, (size_t)n);
#endif
    size_t pos = s.find_last_of("/\\");
    return (pos == std::string::npos) ? std::string(".") : s.substr(0, pos);
}

bool fileExists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

std::string resolveDbPath(const std::string& filename) {
    const char* env = getenv("STOCK_DB_DIR");
    if (env && *env) {
        std::string p = std::string(env) + "/" + filename;
        if (fileExists(p)) return p;
    }
    std::string pexe = exeDir() + "/" + filename;
    if (fileExists(pexe)) return pexe;
    std::string pdev = "/home/srdev/trade/" + filename;
    if (fileExists(pdev)) return pdev;
    return pexe;   // 都不存在时返回 exe 目录路径(供错误提示)
}

Engine::Engine(const std::string& latestDb, const std::string& histDb)
    : latestDb_(latestDb), histDb_(histDb) {}

Engine::~Engine() {
    if (dbLatest_) sqlite3_close(dbLatest_);
    if (dbHist_)   sqlite3_close(dbHist_);
}

bool Engine::open(std::string& err) {
    int rc = sqlite3_open_v2(latestDb_.c_str(), &dbLatest_, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || dbLatest_ == nullptr) {
        err = "打开主库失败: " + latestDb_;
        if (dbLatest_) { sqlite3_close(dbLatest_); dbLatest_ = nullptr; }
        return false;
    }
    // 估值库可选: 打开失败则降级(估值分位返回 -1), 不影响核心功能
    // 带 3 次重试 —— 防启动瞬间被杀毒/索引/同步软件短暂占用
    latestDateCache_.clear();   // 重开数据库后失效
    histOpenErr_.clear();
    for (int attempt = 0; attempt < 3 && dbHist_ == nullptr; attempt++) {
        rc = sqlite3_open_v2(histDb_.c_str(), &dbHist_, SQLITE_OPEN_READONLY, nullptr);
        if (rc == SQLITE_OK && dbHist_ != nullptr) break;
        if (dbHist_) { sqlite3_close(dbHist_); dbHist_ = nullptr; }
        histOpenErr_ = "无法打开: " + histDb_;
#ifdef _WIN32
        Sleep(300);
#else
        usleep(300000);
#endif
    }
    if (dbHist_ != nullptr) detectValTables();
    loadSnapInfo();     // 加载 stocks 表市值/换手缓存
    return true;
}

// 探测估值表名与内容 (open / ensureHistDb 共用)
void Engine::detectValTables() {
    if (!dbHist_) return;
    // 打包库为 valuation(code,d,pe_ttm,pb), 全历史库为 valuation_daily(...,date,...)
    valTable_ = "valuation"; valDateCol_ = "d";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(dbHist_, "SELECT name FROM sqlite_master WHERE type='table' AND name='valuation'", -1, &st, nullptr) == SQLITE_OK) {
        bool found = (sqlite3_step(st) == SQLITE_ROW);
        sqlite3_finalize(st);
        if (!found) { valTable_ = "valuation_daily"; valDateCol_ = "date"; }
    }
    // 探测估值库是否也含K线表 (打包版只有估值表, 跳过历史K线查询提速)
    hasHistKline_ = false;
    if (sqlite3_prepare_v2(dbHist_, "SELECT name FROM sqlite_master WHERE type='table' AND name='kline_daily'", -1, &st, nullptr) == SQLITE_OK) {
        hasHistKline_ = (sqlite3_step(st) == SQLITE_ROW);
        sqlite3_finalize(st);
    }
}

// 惰性连接估值库: 启动时打开失败的场合, 体检/回测使用时自动重连一次
bool Engine::ensureHistDb() {
    if (dbHist_) return true;
    histOpenErr_.clear();
    int rc = sqlite3_open_v2(histDb_.c_str(), &dbHist_, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || dbHist_ == nullptr) {
        if (dbHist_) { sqlite3_close(dbHist_); dbHist_ = nullptr; }
        histOpenErr_ = "无法打开: " + histDb_;
        return false;
    }
    detectValTables();
    return true;
}

// ---------- 读一个库的K线 ----------
static std::vector<Bar> queryBars(sqlite3* db, const std::string& code, int limit, bool desc) {
    std::vector<Bar> out;
    if (!db) return out;
    // 优先带 amount/turnover_rate 列; 旧库缺列时降级为基础列
    auto run = [&](bool extra) -> bool {
        std::string sel = extra ? "date,open,high,low,close,volume,amount,turnover_rate"
                                : "date,open,high,low,close,volume";
        std::string sql = "SELECT " + sel + " FROM kline_daily WHERE code=? ORDER BY date "
                        + std::string(desc ? "DESC" : "ASC")
                        + (limit > 0 ? " LIMIT " + std::to_string(limit) : "");
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st, 1, code.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            Bar b;
            const unsigned char* d = sqlite3_column_text(st, 0);
            b.date = d ? normDate((const char*)d) : "";
            b.open  = sqlite3_column_double(st, 1);
            b.high  = sqlite3_column_double(st, 2);
            b.low   = sqlite3_column_double(st, 3);
            b.close = sqlite3_column_double(st, 4);
            b.volume= sqlite3_column_double(st, 5);
            if (extra) {
                b.amount = sqlite3_column_double(st, 6);
                b.turnover = sqlite3_column_double(st, 7);
            }
            if (!b.date.empty() && b.close > 0) out.push_back(b);
        }
        sqlite3_finalize(st);
        return true;
    };
    if (!run(true)) {
        out.clear();
        run(false);
    }
    if (desc) std::reverse(out.begin(), out.end());
    return out;
}

// 最近 k 个不同交易日期(降序)
std::vector<std::string> Engine::recentMarketDates(int k) {
    std::vector<std::string> out;
    if (!dbLatest_ || k <= 0) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(dbLatest_, "SELECT DISTINCT date FROM kline_daily ORDER BY date DESC LIMIT ?",
                           -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int(st, 1, k);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* d = sqlite3_column_text(st, 0);
        if (d) out.push_back((const char*)d);
    }
    sqlite3_finalize(st);
    return out;
}

// 全市场相对强度 RPS: 一条 SQL 算出每只"最近 days 个交易日涨幅", 再按排名给百分位。
// 用窗口函数在库内完成聚合, 避免为了排名把全市场K线都读进内存。
// RPS=100 最强 / 0 最弱; 例如 RPS>=80 即"跑进全市场前 20%"。
std::map<std::string, double> Engine::marketRps(int days) {
    std::map<std::string, double> out;
    if (!dbLatest_ || days < 5) return out;

    const char* sql =
        "WITH r AS ("
        "  SELECT code, close, ROW_NUMBER() OVER (PARTITION BY code ORDER BY date DESC) AS rn"
        "  FROM kline_daily"
        ") "
        "SELECT code, "
        "  MAX(CASE WHEN rn=1 THEN close END) AS c1, "
        "  MAX(CASE WHEN rn=?1 THEN close END) AS cN "
        "FROM r WHERE rn <= ?1 GROUP BY code";

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(dbLatest_, sql, -1, &st, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int(st, 1, days);

    std::vector<std::pair<std::string, double>> rets;
    rets.reserve(8192);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* cp = sqlite3_column_text(st, 0);
        const double c1 = sqlite3_column_double(st, 1);
        const double cN = sqlite3_column_double(st, 2);
        if (!cp || c1 <= 0 || cN <= 0) continue;
        rets.push_back(std::make_pair(std::string((const char*)cp), (c1 - cN) / cN));
    }
    sqlite3_finalize(st);
    if (rets.empty()) return out;

    std::sort(rets.begin(), rets.end(),
              [](const std::pair<std::string, double>& a, const std::pair<std::string, double>& b) {
                  return a.second < b.second;
              });
    const double n = (double)rets.size();
    for (size_t i = 0; i < rets.size(); ++i)
        out[rets[i].first] = (n <= 1.0) ? 100.0 : (double)i / (n - 1.0) * 100.0;
    return out;
}

std::vector<Bar> Engine::loadSeries(const std::string& code, int maxBars) {
    // 历史库取 maxBars 根 + 最新库全部, 用 map 去重合并
    std::map<std::string, Bar> m;
    if (hasHistKline_) {
        auto hist = queryBars(dbHist_, code, maxBars, true);
        for (auto& b : hist) m[b.date] = b;
    }
    auto lat = queryBars(dbLatest_, code, 0, false);
    for (auto& b : lat) m[b.date] = b;
    std::vector<Bar> out;
    out.reserve(m.size());
    for (auto& kv : m) out.push_back(kv.second);
    // 截取最后 maxBars 根
    if ((int)out.size() > maxBars) out.erase(out.begin(), out.end() - maxBars);
    return out;
}

// ---------- 估值分位 ----------
double Engine::pbPercentile3y(const std::string& code, double currentPb) {
    if (currentPb <= 0) return -1;
    ensureHistDb();                       // 惰性重连(启动时偶发失败的自愈)
    if (!dbHist_) return -1;
    // 近3年: 日期 >= '2023-05-14' 归一化比较 ('20230514')
    std::string sql = "SELECT pb FROM " + valTable_ + " WHERE code=? AND " + valDateCol_ + ">='20230514'";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(dbHist_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, code.c_str(), -1, SQLITE_TRANSIENT);
    int total = 0, cnt = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        double v = sqlite3_column_double(st, 0);
        if (v > 0) { total++; if (v <= currentPb) cnt++; }
    }
    sqlite3_finalize(st);
    if (total == 0) return -1;
    return (double)cnt / total * 100.0;
}

double Engine::latestPe(const std::string& code) {
    ensureHistDb();
    if (!dbHist_) return 0;
    std::string sql = "SELECT pe_ttm FROM " + valTable_ + " WHERE code=? AND pe_ttm>0 ORDER BY "
                    + valDateCol_ + " DESC LIMIT 1";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(dbHist_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, code.c_str(), -1, SQLITE_TRANSIENT);
    double v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_double(st, 0);
    sqlite3_finalize(st);
    return v;
}

// ---------- PE 分位 (近3年) ----------
double Engine::pePercentile3y(const std::string& code, double currentPe) {
    if (currentPe <= 0) return -1;
    ensureHistDb();
    if (!dbHist_) return -1;
    std::string sql = "SELECT pe_ttm FROM " + valTable_ + " WHERE code=? AND " + valDateCol_ + ">='20230514'";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(dbHist_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, code.c_str(), -1, SQLITE_TRANSIENT);
    int total = 0, cnt = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        double v = sqlite3_column_double(st, 0);
        if (v > 0) { total++; if (v <= currentPe) cnt++; }
    }
    sqlite3_finalize(st);
    if (total == 0) return -1;
    return (double)cnt / total * 100.0;
}

// ---------- 筹码分布 (三角分布法, 近260日近似) ----------
ChipDist Engine::chipDist(const std::vector<Bar>& bars, double price, int buckets) {
    ChipDist cd;
    if (bars.empty() || price <= 0 || buckets < 10) return cd;
    double lo = 1e18, hi = 0;
    for (const auto& b : bars) { lo = std::min(lo, b.low); hi = std::max(hi, b.high); }
    if (hi <= lo) return cd;
    std::vector<double> chips(buckets, 0.0);
    double step = (hi - lo) / buckets;
    for (const auto& b : bars) {
        if (b.volume <= 0) continue;
        double typ = (b.open + b.close + b.high + b.low) / 4.0;
        int kLo = (int)((b.low - lo) / step);
        int kHi = (int)((b.high - lo) / step);
        kLo = std::max(0, std::min(buckets - 1, kLo));
        kHi = std::max(0, std::min(buckets - 1, kHi));
        double wsum = 0;
        std::vector<double> w(kHi - kLo + 1, 0.0);
        for (int k = kLo; k <= kHi; k++) {
            double p = lo + (k + 0.5) * step;
            double d;
            if (p <= typ) d = (typ > b.low) ? (p - b.low) / (typ - b.low) : 1.0;
            else d = (b.high > typ) ? (b.high - p) / (b.high - typ) : 1.0;
            if (d < 0) d = 0;
            w[k - kLo] = d;
            wsum += d;
        }
        if (wsum <= 0) {   // 一字板等: 全部计入收盘价桶
            int k = std::max(0, std::min(buckets - 1, (int)((b.close - lo) / step)));
            chips[k] += b.volume;
            continue;
        }
        double scale = b.volume / wsum;
        for (int k = kLo; k <= kHi; k++) chips[k] += w[k - kLo] * scale;
    }
    double total = 0;
    for (double v : chips) total += v;
    if (total <= 0) return cd;
    auto costAt = [&](double pct) -> double {
        double target = total * pct / 100.0, acc = 0;
        for (int k = 0; k < buckets; k++) {
            acc += chips[k];
            if (acc >= target) return lo + (k + 0.5) * step;
        }
        return hi;
    };
    double below = 0;
    for (int k = 0; k < buckets; k++)
        if (lo + (k + 0.5) * step < price) below += chips[k];
    cd.profitRatio = below / total * 100.0;
    cd.cost5 = costAt(5); cd.cost15 = costAt(15); cd.cost50 = costAt(50);
    cd.cost85 = costAt(85); cd.cost95 = costAt(95);
    cd.conc70 = (cd.cost85 + cd.cost15) > 0 ? (cd.cost85 - cd.cost15) / (cd.cost85 + cd.cost15) * 100.0 : 0;
    cd.conc90 = (cd.cost95 + cd.cost5) > 0 ? (cd.cost95 - cd.cost5) / (cd.cost95 + cd.cost5) * 100.0 : 0;
    cd.ok = true;
    return cd;
}

// ---------- 快照缓存 (市值/换手, 来自 stocks 表) ----------
void Engine::loadSnapInfo() {
    snapInfo_.clear();
    if (!dbLatest_) return;
    sqlite3_stmt* st = nullptr;
    // 新库: stocks(code,name,mktcap,fcap,turnover); 旧库无列 -> prepare 失败 -> 空缓存
    if (sqlite3_prepare_v2(dbLatest_, "SELECT code, mktcap, fcap, turnover FROM stocks",
                           -1, &st, nullptr) != SQLITE_OK) return;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* c = sqlite3_column_text(st, 0);
        if (!c) continue;
        SnapInfo si;
        si.mktcap = sqlite3_column_double(st, 1);
        si.fcap = sqlite3_column_double(st, 2);
        si.turnover = sqlite3_column_double(st, 3);
        snapInfo_[(const char*)c] = si;
    }
    sqlite3_finalize(st);
}

double Engine::latestPb(const std::string& code) {
    ensureHistDb();
    if (!dbHist_) return 0;
    std::string sql = "SELECT pb FROM " + valTable_ + " WHERE code=? AND pb>0 ORDER BY " + valDateCol_ + " DESC LIMIT 1";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(dbHist_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, code.c_str(), -1, SQLITE_TRANSIENT);
    double v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_double(st, 0);
    sqlite3_finalize(st);
    return v;
}

// ---------- 快照附加信息(内存查表) ----------
SnapInfo Engine::snapInfoOf(const std::string& code) const {
    SnapInfo si;
    if (!dbLatest_) return si;
    auto it = snapInfo_.find(code);
    if (it != snapInfo_.end()) si = it->second;
    return si;
}

// ---------- 名称 ----------
std::string Engine::stockName(const std::string& code) {
    if (!dbLatest_) return "";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(dbLatest_, "SELECT name FROM stocks WHERE code=?", -1, &st, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_text(st, 1, code.c_str(), -1, SQLITE_TRANSIENT);
    std::string nm;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* p = sqlite3_column_text(st, 0);
        if (p) nm = (const char*)p;
    }
    sqlite3_finalize(st);
    return nm;
}

// ---------- 核心分析 ----------
Stats Engine::analyze(const std::string& code, const std::string& name, double pbCurrent,
                        const AnalyzeParams& params) {
    Stats s;
    s.code = code; s.name = name; s.pbCurrent = pbCurrent;
    auto bars = loadSeries(code, 610);
    if (bars.size() < 60) { s.verdict = "数据不足"; return s; }
    // 停牌/退市过滤: 最后K线距全市场最新交易日超过15天
    {
        std::string latestD = latestMarketDate();
        if (!latestD.empty() && dateDiffDays(bars.back().date, latestD) > 15) {
            s.verdict = "停牌/退市(数据过期)";
            return s;
        }
    }
    // 取最后 600 根
    if ((int)bars.size() > 600) bars.erase(bars.begin(), bars.end() - 600);
    int n = (int)bars.size();

    std::vector<double> c(n), h(n);
    for (int i = 0; i < n; i++) { c[i] = bars[i].close; h[i] = bars[i].high; }
    s.price = c[n-1];

    // 涨跌幅
    std::vector<double> pct(n, 0.0);
    for (int i = 1; i < n; i++)
        if (c[i-1] > 0) pct[i] = (c[i]-c[i-1]) / c[i-1] * 100.0;

    // 急杀事件 + 回测 (口径由 params 控制)
    int hold = params.holdDays; if (hold < 2) hold = 2; if (hold > 20) hold = 20;
    double tpMul = 1.0 + params.takeProfit / 100.0;
    int fixes = 0, wins = 0; double cap = 1.0, worst = 0;
    for (int i = 1; i + hold < n; i++) {
        if (pct[i] <= params.dipThreshold && pct[i] > params.dipFloor) {
            s.events++;
            double buy = c[i], sell = -1;
            for (int k = 1; k <= hold; k++)
                if (h[i+k] >= buy*tpMul) { sell = buy*tpMul; break; }
            if (sell < 0) sell = c[std::min(i+hold, n-1)];
            double r = (sell-buy)/buy*100.0;
            if (r > 0) wins++;
            double mx = 0;
            for (int k = 1; k <= hold; k++) mx = std::max(mx, h[i+k]);
            if (mx >= buy*tpMul) fixes++;
            if (r < worst) worst = r;
            cap *= (1.0 + r/100.0);
        }
    }
    s.fixRate = s.events ? (double)fixes/s.events*100.0 : 0;
    s.backtest = (cap-1)*100.0;
    s.worst = worst;

    // 涨停数
    for (int i = 1; i < n; i++) if (pct[i] >= 9.8) s.ztCount++;

    // 均线
    auto ma = [&](int m) { double ssum=0; for (int i=n-m; i<n; i++) ssum += c[i]; return ssum/m; };
    s.ma5 = ma(5); s.ma10 = ma(10); s.ma20 = ma(20); s.ma60 = ma(60);
    s.vsMa20 = (c[n-1]-s.ma20)/s.ma20*100.0;

    // 60日区间位置
    double hi60 = h[n-60], lo60 = 1e18;
    for (int i = n-60; i < n; i++) { hi60 = std::max(hi60, h[i]); lo60 = std::min(lo60, bars[i].low); }
    s.pos60 = (hi60 > lo60) ? (c[n-1]-lo60)/(hi60-lo60)*100.0 : 0;

    // 估值 (pbCurrent 未注入时, 用估值库内最新一条 PB 近似)
    if (pbCurrent <= 0) pbCurrent = latestPb(code);
    if (pbCurrent > 0) { s.pbCurrent = pbCurrent; s.pbPct3y = pbPercentile3y(code, pbCurrent); }

    // ---------- 扩展指标 ----------
    if (n >= 6) {
        double v5 = 0;
        for (int i = n - 6; i < n - 1; i++) v5 += bars[i].volume;
        s.volRatio = v5 > 0 ? bars[n-1].volume / (v5 / 5.0) : 0;
        s.ret5 = bars[n-6].close > 0 ? (bars[n-1].close - bars[n-6].close) / bars[n-6].close * 100.0 : 0;
    }
    if (n >= 21) {
        s.ret20 = bars[n-21].close > 0 ? (bars[n-1].close - bars[n-21].close) / bars[n-21].close * 100.0 : 0;
    }
    if (n >= 2 && bars[n-2].close > 0) {
        s.amp = (bars[n-1].high - bars[n-1].low) / bars[n-2].close * 100.0;
    }
    {
        double h60 = 0;
        for (int i = n - 60; i < n; i++) h60 = std::max(h60, bars[i].high);
        s.fromHigh60 = h60 > 0 ? (bars[n-1].close - h60) / h60 * 100.0 : 0;
        double a5 = 0;
        int ac = 0;
        for (int i = (n >= 5 ? n - 5 : 0); i < n; i++) { a5 += bars[i].amount; ac++; }
        s.amount5 = ac > 0 ? a5 / ac / 1e6 : 0;      // 库内单位 -> 亿元
    }
    {
        auto it = snapInfo_.find(code);
        if (it != snapInfo_.end()) {
            s.turnover = it->second.turnover;
            s.mktcap = it->second.mktcap;
        }
    }
    {
        // 动能: 连续红柱天数 (EMA5 of 涨跌幅% x 成交量 x 收盘价, 近60根)
        int lo = (n > 60) ? n - 60 : 0;
        std::vector<double> ema;
        ema.reserve(n - lo);
        double alpha = 2.0 / 6.0;
        bool first = true;
        double last = 0;
        for (int i = lo; i < n; i++) {
            double pc = (i > 0 && bars[i-1].close > 0)
                      ? (bars[i].close - bars[i-1].close) / bars[i-1].close * 100.0 : 0;
            double pw = pc * bars[i].volume * bars[i].close;
            last = first ? pw : alpha * pw + (1 - alpha) * last;
            ema.push_back(last);
            first = false;
        }
        int streak = 0;
        for (int i = (int)ema.size() - 1; i >= 0 && ema[i] >= 0; i--) streak++;
        s.emaRedDays = streak;
    }

    // 筹码分布 (近260根三角分布法, 供筛选/表格用)
    {
        std::vector<Bar> chipBars;
        int cw = std::min(260, (int)bars.size());
        chipBars.assign(bars.end() - cw, bars.end());
        ChipDist cd = chipDist(chipBars, s.price);
        if (cd.ok) {
            s.chipProfit = cd.profitRatio;
            s.chipConc = cd.conc70;
            s.chipConc90 = cd.conc90;
            s.chipCost50 = cd.cost50;
        }
    }

    // 状态判定
    bool bull = s.ma5 > s.ma20 && s.ma20 > s.ma60;
    bool bear = s.ma5 < s.ma20 && s.ma20 < s.ma60;
    if (s.pos60 < 25 && bear)         s.verdict = "下跌趋势等企稳";
    else if (s.pos60 < 35 && s.vsMa20 > -3) s.verdict = "低位观察位";
    else if (s.pos60 >= 80)           s.verdict = "高位等回踩";
    else if (s.vsMa20 > 3)            s.verdict = "偏强中";
    else                              s.verdict = "中位等触发";
    s.ok = true;
    return s;
}

// ---------- 行业/概念 (简版CSV, 简单解析) ----------
bool Engine::loadMaps(const std::string& industryCsv, const std::string& conceptCsv) {
    auto load = [](const std::string& path, std::map<std::string, std::string>& out) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        char line[4096];
        bool first = true;
        while (fgets(line, sizeof(line), f)) {
            std::string L(line);
            while (!L.empty() && (L.back()=='\n' || L.back()=='\r')) L.pop_back();
            if (first) { first = false; continue; } // 表头
            if (L.size() < 5) continue;
            // 处理 UTF-8 BOM
            if (L.size() >= 3 && (unsigned char)L[0]==0xEF && (unsigned char)L[1]==0xBB && (unsigned char)L[2]==0xBF)
                L = L.substr(3);
            size_t p1 = L.find(',');
            if (p1 == std::string::npos) continue;
            std::string code6 = L.substr(0, p1);
            // 名称是第2列, 值取最后一列
            size_t last = L.rfind(',');
            std::string val = (last != std::string::npos) ? L.substr(last+1) : "";
            out[code6] = val;
        }
        fclose(f);
        return true;
    };
    load(industryCsv, industry_);
    load(conceptCsv, concepts_);
    return true;
}

// ---------- 行业/概念查询 ----------
static std::string code6of(const std::string& code) {
    size_t dot = code.find('.');
    return (dot != std::string::npos) ? code.substr(0, dot) : code;
}

std::string Engine::industryOf(const std::string& code) {
    auto it = industry_.find(code6of(code));
    return it != industry_.end() ? it->second : std::string();
}

std::string Engine::conceptsOf(const std::string& code) {
    auto it = concepts_.find(code6of(code));
    return it != concepts_.end() ? it->second : std::string();
}

// ---------- 全部股票列表 ----------
std::vector<std::pair<std::string, std::string>> Engine::allStocks() {
    std::vector<std::pair<std::string, std::string>> out;
    if (!dbLatest_) return out;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(dbLatest_, "SELECT code, name FROM stocks ORDER BY code", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* c = sqlite3_column_text(st, 0);
            const unsigned char* n = sqlite3_column_text(st, 1);
            if (c) out.emplace_back((const char*)c, n ? (const char*)n : "");
        }
        sqlite3_finalize(st);
    }
    if (out.empty()) {   // fallback: 从K线表取代码
        if (sqlite3_prepare_v2(dbLatest_, "SELECT DISTINCT code FROM kline_daily ORDER BY code", -1, &st, nullptr) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const unsigned char* c = sqlite3_column_text(st, 0);
                if (c) out.emplace_back((const char*)c, "");
            }
            sqlite3_finalize(st);
        }
    }
    return out;
}
