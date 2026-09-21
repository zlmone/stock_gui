// news.cpp — 财经新闻采集核心实现 (纯 C++ / SQLite, 不依赖 Qt, 便于单元测试)
#include "news.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <algorithm>
#include <sqlite3.h>

// ===================== URL 编码 =========================
static bool isUnreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
}
static std::string urlencode(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (unsigned char c : s) {
        if (isUnreserved(c)) o += (char)c;
        else if (c == ' ') o += '+';
        else { char b[4]; snprintf(b, sizeof(b), "%%%02X", c); o += b; }
    }
    return o;
}

std::string buildFeedUrl(const NewsQuery& q) {
    if (!q.url.empty()) return q.url;
    std::string hl = q.lang.empty() ? "zh-CN" : q.lang;
    std::string gl = q.region.empty() ? "CN" : q.region;
    std::string ceid = gl + ":" + (hl == "en" ? "en" : "zh-Hans");
    return "https://news.google.com/rss/search?q=" + urlencode(q.keyword)
         + "&hl=" + hl + "&gl=" + gl + "&ceid=" + ceid;
}

std::vector<NewsQuery> defaultQueries() {
    std::vector<NewsQuery> q;
    { NewsQuery a; a.keyword = "股票 OR A股 OR 财经 OR 券商 OR 央行"; a.source = "Google News";
      a.category = "国内"; a.lang = "zh-CN"; a.region = "CN"; q.push_back(a); }
    { NewsQuery b; b.keyword = "stock market OR economy OR Federal Reserve OR inflation OR tariffs";
      b.source = "Google News"; b.category = "国际"; b.lang = "en"; b.region = "US"; q.push_back(b); }
    { NewsQuery c; c.keyword = "美联储 OR 经济 OR 汇率 OR 黄金 OR 原油 OR 芯片"; c.source = "Google News";
      c.category = "宏观"; c.lang = "zh-CN"; c.region = "CN"; q.push_back(c); }
    // 固定源兜底 (Google News 失败仍有 CBC/MarketWatch 可用)
    { NewsQuery d; d.category = "国际"; d.lang = "en"; d.region = "US";
      d.url = "https://www.cnbc.com/id/100003114/device/rss/rss.html"; q.push_back(d); }
    { NewsQuery e; e.category = "国际"; e.lang = "en"; e.region = "US";
      e.url = "https://www.marketwatch.com/rss/topstories"; q.push_back(e); }
    return q;
}

// ===================== XML 解析工具 =========================
static inline bool isWs(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; }

static std::string decodeEntities(const std::string& s) {
    std::string o = s;
    struct Rep { const char* from; const char* to; };
    static const Rep reps[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&nbsp;", " "},
    };
    for (auto& r : reps) {
        size_t p = 0;
        while ((p = o.find(r.from, p)) != std::string::npos) {
            o.replace(p, std::string(r.from).size(), r.to);
            p += std::string(r.to).size();
        }
    }
    return o;
}

// 去除 CDATA 包裹与 HTML 标签, 并去首尾空白
static std::string cleanText(const std::string& s) {
    std::string r = s;
    size_t c1 = r.find("<![CDATA[");
    if (c1 != std::string::npos) {
        size_t c2 = r.find("]]>", c1);
        r = (c2 != std::string::npos) ? r.substr(c1 + 9, c2 - c1 - 9) : r.substr(c1 + 9);
    }
    // 先解码实体 (&lt; -> <), 再去标签, 否则编码过的 HTML 标签残留
    std::string txt = decodeEntities(r);
    std::string out; out.reserve(txt.size());
    bool inTag = false;
    for (char c : txt) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) out += c;
    }
    size_t a = 0, b = out.size();
    while (a < b && isWs(out[a])) a++;
    while (b > a && isWs(out[b - 1])) b--;
    return out.substr(a, b - a);
}

// 查找 '<tag' 起始位置 (tag 为裸名, 忽略命名空间前缀, 大小写不敏感)
static size_t findTag(const std::string& s, const std::string& tag, size_t from) {
    size_t p = from;
    while (true) {
        size_t q = s.find('<', p);
        if (q == std::string::npos) return std::string::npos;
        size_t k = q + 1;
        while (k < s.size() && (isalpha((unsigned char)s[k]) || s[k] == ':')) k++;
        bool match = (s.substr(q + 1, k - q - 1) == tag)
                  && (k >= s.size() || isWs(s[k]) || s[k] == '>' || s[k] == '/');
        if (match) return q;
        p = q + 1;
    }
}

static std::string extractTag(const std::string& s, const std::string& tag, size_t from = 0) {
    size_t o = findTag(s, tag, from);
    if (o == std::string::npos) return "";
    size_t gt = s.find('>', o + 1);
    if (gt == std::string::npos) return "";
    size_t c = s.find("</" + tag, gt + 1);
    if (c == std::string::npos) return "";
    return cleanText(s.substr(gt + 1, c - gt - 1));
}

// 取标签属性 (name="x" 或 name='x')
static std::string attrOf(const std::string& attrStr, const std::string& name) {
    size_t p = attrStr.find(" " + name + "=\"");
    if (p != std::string::npos) {
        p += (" " + name + "=\"").size();
        size_t e = attrStr.find('"', p);
        return e == std::string::npos ? "" : attrStr.substr(p, e - p);
    }
    p = attrStr.find(" " + name + "='");
    if (p != std::string::npos) {
        p += (" " + name + "='").size();
        size_t e = attrStr.find('\'', p);
        return e == std::string::npos ? "" : attrStr.substr(p, e - p);
    }
    return "";
}

static bool isUrl(const std::string& s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

// 取条目链接: 优先 <link>..</link>, 其次 <link href=.../> (prefer rel=alternate)
static std::string extractLink(const std::string& item) {
    std::string t = extractTag(item, "link");
    if (!t.empty() && isUrl(t)) return t;
    std::string bestRel, bestPlain;
    size_t pos = 0;
    while (true) {
        size_t o = findTag(item, "link", pos);
        if (o == std::string::npos) break;
        size_t end = item.find('>', o + 1);
        if (end == std::string::npos) break;
        std::string attr = item.substr(o + 1, end - o - 1);
        std::string href = attrOf(attr, "href");
        std::string rel = attrOf(attr, "rel");
        if (isUrl(href)) {
            if (rel.empty() || rel == "alternate") { bestRel = href; break; }
            if (bestPlain.empty()) bestPlain = href;
        }
        pos = end + 1;
    }
    if (!bestRel.empty()) return bestRel;
    return bestPlain;
}

// 取 <source ...> 的来源名 (name 属性优先, 否则标签内文本)
static std::string extractSource(const std::string& item) {
    size_t o = findTag(item, "source", 0);
    if (o == std::string::npos) return "";
    size_t gt = item.find('>', o + 1);
    if (gt == std::string::npos) return "";
    std::string attr = item.substr(o + 1, gt - o - 1);
    std::string nm = attrOf(attr, "name");
    if (!nm.empty()) return nm;
    if (gt + 1 < item.size() && item[gt + 1] == '/') return "";
    size_t c = item.find("</source>", gt + 1);
    std::string inner = (c == std::string::npos) ? "" : item.substr(gt + 1, c - gt - 1);
    return cleanText(inner);
}

// 尝试若干日期格式, 返回 unix 秒 (失败 0)
// Portable date parser: mingw has no strptime.  Supports only the directives
// used by the callers (%Y %m %d %H %M %S %b %a %z).  %z/%a are skipped because
// mktime ignores gmtoff; this matches libc strptime + mktime epoch values.
static int news_read_int(const char** pp) {
    const char* p = *pp;
    while (*p && !isdigit((unsigned char)*p) && *p != '-') p++;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    long v = 0;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; }
    *pp = p;
    return sign < 0 ? -(int)v : (int)v;
}
static int news_read_mon(const char** pp) {
    static const char* nm[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
    const char* q = *pp;
    while (*q && isspace((unsigned char)*q)) q++;
    for (int i = 0; i < 12; i++) {
        if (strncasecmp(q, nm[i], 3) == 0) { *pp = q + 3; return i; }
    }
    *pp = q;
    return -1;
}
static bool news_strptime(const char* s, const char* fmt, struct tm* tm) {
    const char* p = s;
    while (*fmt) {
        char c = *fmt++;
        if (c == '%') {
            char f = *fmt++;
            switch (f) {
                case 'Y': tm->tm_year = news_read_int(&p) - 1900; break;
                case 'm': tm->tm_mon  = news_read_int(&p) - 1;   break;
                case 'd': tm->tm_mday = news_read_int(&p);       break;
                case 'H': tm->tm_hour = news_read_int(&p);       break;
                case 'M': tm->tm_min  = news_read_int(&p);       break;
                case 'S': tm->tm_sec  = news_read_int(&p);       break;
                case 'b': { int m = news_read_mon(&p); if (m < 0) return false; tm->tm_mon = m; } break;
                case 'z':
                case 'a':
                    while (*p && isspace((unsigned char)*p)) p++;
                    while (*p && !isspace((unsigned char)*p)) p++;
                    break;
                default: break;
            }
        } else {
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == c) p++;
        }
    }
    return true;
}
static long long tryParseDate(const std::string& raw) {

    if (raw.empty()) return 0;
    if (raw.size() >= 200) return 0;
    char buf[200];
    memcpy(buf, raw.c_str(), raw.size());
    buf[raw.size()] = '\0';
    static const char* fmts[] = {
        "%a, %d %b %Y %H:%M:%S %z", "%a, %d %b %Y %H:%M:%S GMT", "%a, %d %b %Y %H:%M:%S",
        "%Y-%m-%dT%H:%M:%SZ",        "%Y-%m-%dT%H:%M:%S%z",        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%d %H:%M:%S",         "%Y-%m-%d",
    };
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    for (const char* f : fmts) {
        if (news_strptime(buf, f, &tm)) return (long long)mktime(&tm);
    }
    return 0;
}

static long long dateOfField(const std::string& item, const std::vector<std::string>& tags) {
    for (const std::string& t : tags) {
        std::string v = cleanText(extractTag(item, t));
        long long e = tryParseDate(v);
        if (e > 0) return e;
    }
    return 0;
}

std::vector<NewsItem> parseFeed(const std::string& body, const std::string& source,
                                const std::string& category) {
    std::vector<NewsItem> out;
    size_t pos = 0;
    while (true) {
        size_t oI = findTag(body, "item", pos);      // RSS2.0 <item>
        size_t oE = findTag(body, "entry", pos);     // Atom <entry>
        size_t o = std::string::npos;
        if (oI != std::string::npos && (oE == std::string::npos || oI < oE)) o = oI;
        else if (oE != std::string::npos) o = oE;
        else break;
        size_t next = std::string::npos;
        size_t a = findTag(body, "entry", o + 1);
        size_t b = findTag(body, "item", o + 1);
        if (a != std::string::npos) next = a;
        if (b != std::string::npos && (next == std::string::npos || b < next)) next = b;
        std::string item = body.substr(o, (next == std::string::npos) ? std::string::npos : (next - o));

        NewsItem it;
        it.source = extractSource(item);
        if (it.source.empty()) it.source = source;
        it.category = category;
        it.title = decodeEntities(cleanText(extractTag(item, "title")));
        it.link = extractLink(item);
        it.epoch = dateOfField(item, {"pubDate", "published", "issued", "updated", "date", "dc:date"});
        it.description = cleanText(extractTag(item, "description") + extractTag(item, "summary"));
        if (!it.title.empty() || !it.link.empty()) out.push_back(it);
        pos = next;
    }
    return out;
}

std::vector<NewsItem> dedupItems(std::vector<NewsItem> items) {
    std::set<std::string> seen;
    std::vector<NewsItem> out;
    for (auto& it : items) {
        if (it.link.empty()) { out.push_back(it); continue; }
        if (seen.count(it.link)) continue;
        seen.insert(it.link);
        out.push_back(it);
    }
    return out;
}

// ===================== SQLite 存储 =========================
static void ensureSchema(sqlite3* db) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS news_items("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  link TEXT UNIQUE NOT NULL,"
        "  title TEXT,"
        "  source TEXT,"
        "  category TEXT,"
        "  description TEXT,"
        "  epoch INTEGER);"
        "CREATE INDEX IF NOT EXISTS idx_news_cat ON news_items(category);"
        "CREATE INDEX IF NOT EXISTS idx_news_epoch ON news_items(epoch DESC);"
        "CREATE INDEX IF NOT EXISTS idx_news_title ON news_items(title);";
    char* err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

static const char* col(sqlite3_stmt* st, int i) {
    const unsigned char* p = sqlite3_column_text(st, i);
    return p ? (const char*)p : "";
}

int cacheNews(const std::string& dbPath, std::vector<NewsItem>& items) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        return 0;
    ensureSchema(db);
    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO news_items(link,title,source,category,description,epoch) "
        "VALUES(?,?,?,?,?,?)", -1, &ins, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return 0;
    }
    int inserted = 0;
    for (auto& it : items) {
        sqlite3_bind_text(ins, 1, it.link.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 2, it.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, it.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 4, it.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 5, it.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(ins, 6, (sqlite3_int64)it.epoch);
        sqlite3_step(ins);                 // INSERT 返回 SQLITE_DONE
        if (sqlite3_changes(db) > 0) inserted++;  // OR IGNORE 去重: 新增才计入
        sqlite3_reset(ins);
        sqlite3_clear_bindings(ins);
    }
    sqlite3_finalize(ins);
    sqlite3_close(db);
    return inserted;
}

std::vector<NewsItem> loadNews(const std::string& dbPath, const std::string& categoryFilter) {
    std::vector<NewsItem> out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) return out;
    std::string sql;
    sqlite3_stmt* st = nullptr;
    if (categoryFilter.empty()) {
        sql = "SELECT link, title, source, category, description, epoch "
              "FROM news_items ORDER BY epoch DESC, id DESC LIMIT 500";
    } else {
        sql = "SELECT link, title, source, category, description, epoch "
              "FROM news_items WHERE category=?1 "
              "ORDER BY epoch DESC, id DESC LIMIT 500";
    }
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
        sqlite3_close(db); return out;
    }
    if (!categoryFilter.empty()) {
        sqlite3_bind_text(st, 1, categoryFilter.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        NewsItem it;
        it.link = col(st, 0);
        it.title = col(st, 1);
        it.source = col(st, 2);
        it.category = col(st, 3);
        it.description = col(st, 4);
        it.epoch = (long long)sqlite3_column_int64(st, 5);
        out.push_back(it);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

std::vector<NewsItem> searchNews(const std::string& dbPath, const std::string& keyword, int limit) {
    std::vector<NewsItem> out;
    if (keyword.empty()) return loadNews(dbPath, "");
    std::string like; like += "%";
    for (char c : keyword) { if (c == '%' || c == '_') { like += '\\'; like += c; } else like += c; }
    like += "%";
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) return out;
    const char* sql =
        "SELECT link, title, source, category, description, epoch FROM news_items WHERE "
        "title LIKE ?1 ESCAPE '\\' OR description LIKE ?1 ESCAPE '\\' OR "
        "source LIKE ?1 ESCAPE '\\' OR category LIKE ?1 ESCAPE '\\' "
        "ORDER BY epoch DESC, id DESC LIMIT ?";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        sqlite3_close(db); return out;
    }
    sqlite3_bind_text(st, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        NewsItem it;
        it.link = col(st, 0);
        it.title = col(st, 1);
        it.source = col(st, 2);
        it.category = col(st, 3);
        it.description = col(st, 4);
        it.epoch = (long long)sqlite3_column_int64(st, 5);
        out.push_back(it);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}
