// news.cpp — 财经新闻采集核心实现 (纯 C++ / SQLite, 不依赖 Qt, 便于单元测试)
#include "news.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctype.h>
#include <ctime>
#include <map>
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
    // 说明: 静态 Qt 现在构建时已启用 Windows Schannel TLS 后端
    //   (配方见 scripts/build_static_qt_tls.sh), 产物 exe 会导入 Secur32/Crypt32,
    //   具备完整 https 能力, 因此 https 源同样可用。
    //   下面同时保留一批实测可用的 http:// 源作冗余: 只要有任一源成功,
    //   updateNews 的 okCount 就 >0, 不会因单源故障而整体显示"新闻源暂时不可用"。
    { NewsQuery a; a.source = "新浪财经"; a.category = "国内"; a.lang = "zh-CN"; a.region = "CN";
      a.url = "http://feed.mix.sina.com.cn/api/roll/get?pageid=153&lid=2510&num=30&page=1"; q.push_back(a); }
    { NewsQuery b; b.source = "新浪财经"; b.category = "国际"; b.lang = "zh-CN"; b.region = "CN";
      b.url = "http://feed.mix.sina.com.cn/api/roll/get?pageid=153&lid=2511&num=30&page=1"; q.push_back(b); }
    { NewsQuery c; c.source = "东方财富"; c.category = "国内"; c.lang = "zh-CN"; c.region = "CN";
      c.url = "http://np-listapi.eastmoney.com/comm/web/getNewsByColumns?client=web&biz=web_home_channel&column=350,35,466,467&order=1&needInteractData=0&page_index=1&page_size=30&req_trace=21e65da9-1234-4567-89ab-0123456789ab"; q.push_back(c); }
    { NewsQuery d; d.source = "华尔街见闻"; d.category = "宏观"; d.lang = "zh-CN"; d.region = "CN";
      d.url = "http://api-one.wallstcn.com/apiv1/content/lives?channel=global-channel&limit=30"; q.push_back(d); }
    { NewsQuery e; e.source = "华尔街见闻"; e.category = "国内"; e.lang = "zh-CN"; e.region = "CN";
      e.url = "http://api-one.wallstcn.com/apiv1/content/lives?channel=a-stock-channel&limit=30"; q.push_back(e); }
    { NewsQuery f; f.source = "财联社"; f.category = "国内"; f.lang = "zh-CN"; f.region = "CN";
      f.url = "https://www.cls.cn/api/cache?app=CailianpressWeb&name=telegraph&os=web&sv=8.7.9"; q.push_back(f); }
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


// ===================== 极简 JSON 解析器 (自包含, 无第三方依赖) =====================
// 供国内 JSON 新闻源 (财联社/华尔街见闻/东方财富) 解析. 容错: 解析中断只返回已解析部分.
struct JsonValue {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool bval = false;
    double num = 0;
    std::string sval;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;
    const JsonValue* find(const std::string& k) const {
        if (type != Obj) return nullptr;
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
};

struct JsonParser {
    const char* p; const char* end;
    JsonParser(const std::string& s): p(s.c_str()), end(p + (int)s.size()) {}
    void skipWs(){ while(p<end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) p++; }
    bool ok(){ skipWs(); return p>=end; }
    bool value(JsonValue& v){
        skipWs();
        if(p>=end) return false;
        char c=*p;
        if(c=='{') return object(v);
        if(c=='[') return array(v);
        if(c=='"'){ v.type=JsonValue::Str; v.sval=lexString(); return true; }
        if(c=='t'||c=='f') return boolean(v);
        if(c=='n') return nullv(v);
        return number(v);
    }
    bool object(JsonValue& v){
        v.type=JsonValue::Obj; p++;
        skipWs();
        if(p<end && *p=='}'){ p++; return true; }
        while(p<end){
            skipWs();
            if(p>=end || *p!='"') return false;
            std::string key=lexString();
            skipWs();
            if(p>=end || *p!=':') return false;
            p++;
            JsonValue val; if(!value(val)) return false;
            v.obj.emplace(std::move(key), std::move(val));
            skipWs();
            if(p<end && *p==','){ p++; continue; }
            if(p<end && *p=='}'){ p++; return true; }
            return false;
        }
        return false;
    }
    bool array(JsonValue& v){
        v.type=JsonValue::Arr; p++;
        skipWs();
        if(p<end && *p==']'){ p++; return true; }
        while(p<end){
            JsonValue el; if(!value(el)) return false;
            v.arr.push_back(std::move(el));
            skipWs();
            if(p<end && *p==','){ p++; continue; }
            if(p<end && *p==']'){ p++; return true; }
            return false;
        }
        return false;
    }
    std::string lexString(){
        p++; std::string s;
        while(p<end && *p!='"'){
            char c=*p++;
            if(c=='\\'){
                if(p>=end) break;
                char e=*p++;
                switch(e){
                    case '"': s+='"'; break;
                    case '\\': s+='\\'; break;
                    case '/': s+='/'; break;
                    case 'b': s+='\b'; break;
                    case 'f': s+='\f'; break;
                    case 'n': s+='\n'; break;
                    case 'r': s+='\r'; break;
                    case 't': s+='\t'; break;
                    case 'u': {
                        // 正确的 \uXXXX 解码(含 UTF-16 代理对), 按 UTF-8 写入.
                        // 原实现只跳过 4 个十六进制位而不还原字符, 导致新浪等
                        // "整串 \uXXXX 转义"的 JSON 标题被解析成空串 —— 这正是大量
                        // 新闻条目无标题的根因(纯中文标题全空, 含 ASCII 的则中文被吃掉).
                        unsigned cp = 0; bool bad = false;
                        if(p+4 > end) bad = true;
                        else for(int k=0;k<4;k++){
                            char h=p[k]; cp<<=4;
                            if(h>='0'&&h<='9') cp|=(unsigned)(h-'0');
                            else if(h>='a'&&h<='f') cp|=(unsigned)(h-'a'+10);
                            else if(h>='A'&&h<='F') cp|=(unsigned)(h-'A'+10);
                            else { bad=true; break; }
                        }
                        if(!bad){
                            p+=4;
                            if(cp>=0xD800 && cp<=0xDBFF && p+6<=end && p[0]=='\\' && p[1]=='u'){
                                unsigned lo=0; bool ok=true;
                                for(int k=0;k<4;k++){
                                    char h=p[2+k]; lo<<=4;
                                    if(h>='0'&&h<='9') lo|=(unsigned)(h-'0');
                                    else if(h>='a'&&h<='f') lo|=(unsigned)(h-'a'+10);
                                    else if(h>='A'&&h<='F') lo|=(unsigned)(h-'A'+10);
                                    else { ok=false; break; }
                                }
                                if(ok && lo>=0xDC00 && lo<=0xDFFF){ cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00); p+=6; }
                            }
                            if(cp<0x80) s+=(char)cp;
                            else if(cp<0x800){ s+=(char)(0xC0|(cp>>6)); s+=(char)(0x80|(cp&0x3F)); }
                            else if(cp<0x10000){ s+=(char)(0xE0|(cp>>12)); s+=(char)(0x80|((cp>>6)&0x3F)); s+=(char)(0x80|(cp&0x3F)); }
                            else { s+=(char)(0xF0|(cp>>18)); s+=(char)(0x80|((cp>>12)&0x3F)); s+=(char)(0x80|((cp>>6)&0x3F)); s+=(char)(0x80|(cp&0x3F)); }
                        }
                        break;
                    }
                    default: s+=e; break;
                }
            } else s+=c;
        }
        if(p<end) p++;
        return s;
    }
    bool boolean(JsonValue& v){
        if(p+4<=end && std::string(p,4)=="true"){ p+=4; v.type=JsonValue::Bool; v.bval=true; return true; }
        if(p+5<=end && std::string(p,5)=="false"){ p+=5; v.type=JsonValue::Bool; v.bval=false; return true; }
        return false;
    }
    bool nullv(JsonValue& v){
        if(p+4<=end && std::string(p,4)=="null"){ p+=4; v.type=JsonValue::Null; return true; }
        return false;
    }
    bool number(JsonValue& v){
        const char* s=p;
        if(p<end && (*p=='-'||*p=='+')) p++;
        while(p<end && (isdigit((unsigned char)*p)||*p=='.'||*p=='e'||*p=='E'||*p=='+'||*p=='-')) p++;
        if(p==s) return false;
        v.type=JsonValue::Num; v.num=std::atof(s); return true;
    }
};

static long long epochFrom(const JsonValue* f){
    if(!f) return 0;
    if(f->type == JsonValue::Num) return (long long)f->num;
    if(f->type == JsonValue::Str && !f->sval.empty()){
        bool digits=!f->sval.empty();
        for(unsigned char c: f->sval) if(!isdigit(c)){ digits=false; break; }
        if(digits) return (long long)atoll(f->sval.c_str());
        long long e=tryParseDate(f->sval);
        if(e>0) return e;
    }
    return 0;
}

std::vector<NewsItem> parseJsonFeed(const std::string& body, const std::string& source, const std::string& category){
    std::vector<NewsItem> out;
    JsonParser jp(body);
    JsonValue root;
    if(!jp.value(root) || !jp.ok()) return out;

    const JsonValue* src = root.find("data");
    if(!src) src = root.find("result");
    if(!src) src = &root;

    // 找包含条目的数组: 优先已知键, 否则 data 下首个 对象数组
    static const char* keys[] = {"roll_data","items","list","results","content","news","messages","data", nullptr};
    const JsonValue* arr = nullptr;
    for(int i=0; keys[i]; i++){
        const JsonValue* c = src->find(keys[i]);
        if(c && c->type==JsonValue::Arr && !c->arr.empty() && c->arr[0].type==JsonValue::Obj){ arr=c; break; }
    }
    if(!arr && src->type==JsonValue::Obj){
        for(auto& kv: src->obj)
            if(kv.second.type==JsonValue::Arr && !kv.second.arr.empty() && kv.second.arr[0].type==JsonValue::Obj){ arr=&kv.second; break; }
    }
    if(!arr) return out;

    static const char* tkeys[] = {"title","text","content_text","brief","summary","content","digest","description","headline", nullptr};
    static const char* lkeys[] = {"url","link","uniqueUrl","uri","detail_url","content_url","urlName","feedUrl", nullptr};
    static const char* kkeys[] = {"ctime","display_time","showTime","create_time","publish_time","pushTime","ctimeStr", nullptr};

    for(auto& item: arr->arr){
        NewsItem it;
        it.source = source;
        it.category = category;
        std::string title, desc;
        for(int i=0; tkeys[i]; i++){ const JsonValue* f=item.find(tkeys[i]); if(f && f->type==JsonValue::Str && !f->sval.empty()){ if(title.empty()) title=f->sval; if(desc.empty()) desc=f->sval; break; } }
        it.title = title;
        it.description = desc;
        for(int i=0; lkeys[i]; i++){ const JsonValue* f=item.find(lkeys[i]); if(f && f->type==JsonValue::Str && !f->sval.empty()){ it.link=f->sval; break; } }
        // 部分快讯源(如财联社)不返回 url 字段, 只给数字 id。
        // news_items.link 是 UNIQUE NOT NULL, 留空会让同源条目互相冲突而只剩一条,
        // 故按官方详情页规则补全成稳定唯一的链接。
        if(it.link.empty() && source == "财联社"){
            const JsonValue* idv = item.find("id");
            std::string ids;
            if(idv && idv->type==JsonValue::Str) ids = idv->sval;
            else if(idv && idv->type==JsonValue::Num) ids = std::to_string((long long)idv->num);
            if(!ids.empty()) it.link = "https://www.cls.cn/detail/" + ids;
        }
        long long ep=0;
        for(int i=0; kkeys[i]; i++){ ep=epochFrom(item.find(kkeys[i])); if(ep>0) break; }
        while(ep>1000000000000LL) ep/=1000;   // 毫秒->秒 (如 wallstcn display_time)
        it.epoch = ep;
        if(!it.title.empty() || !it.link.empty()) out.push_back(it);
    }
    return out;
}

std::vector<NewsItem> collectItems(const std::string& body, const std::string& source, const std::string& category){
    for(unsigned char c: body){ if(isWs(c)) continue; return (c=='{') ? parseJsonFeed(body, source, category) : parseFeed(body, source, category); }
    return {};
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
