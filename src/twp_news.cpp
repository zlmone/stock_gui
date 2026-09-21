// twp_news.cpp — 新闻采集核心离线单测 (纯 C++, 不联网, 不依赖 Qt)
#include "news.h"
#include <cstdio>
#include <string>

static int g_fail = 0;
static void ck(bool cond, const char* what) {
    printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) g_fail++;
}

// GoogleNews 形状 (RSS2.0): <source name="..">, CDATA, HTML 描述, 时间戳
static const std::string kGoogleNews = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0">
<channel>
  <title>Google News</title>
  <item>
    <title>美联储维持利率不变, 通胀担忧升温 - Reuters</title>
    <guid isPermaLink="false">guid1</guid>
    <description>&lt;p&gt;Fed holds rates as inflation concerns grow&lt;/p&gt;</description>
    <pubDate>Mon, 06 Oct 2025 10:00:00 GMT</pubDate>
    <link>https://news.google.com/rss/link/AAA</link>
    <source url="https://www.reuters.com/" name="Reuters">Reuters</source>
  </item>
  <item>
    <title>A股大涨 沪指创阶段新高 - 新浪财经</title>
    <pubDate>Tue, 07 Oct 2025 09:30:00 +0800</pubDate>
    <link><![CDATA[https://news.google.com/rss/link/BBB]]></link>
    <source name="新浪财经">新浪财经</source>
  </item>
  <item>
    <title>无来源字段应回退到 feed 源 - AP News</title>
    <link>https://news.google.com/rss/link/CCC</link>
    <pubDate>Sat, 04 Oct 2025 08:00:00 GMT</pubDate>
  </item>
</channel>
</rss>)XML";

// Atom 形状: <link href=.. rel=alternate>, <published>
static const std::string kAtom = R"XML(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>CNBC</title>
  <entry>
    <title>Wall Street rallies on jobs data</title>
    <link href="https://www.cnbc.com/2025/10/07/rally.html" rel="alternate"/>
    <published>2025-10-07T14:15:30Z</published>
    <summary>A modest jobs report boosted sentiment.</summary>
  </entry>
</feed>)XML";

int main() {
    // ---- parseFeed: GoogleNews 形状 ----
    auto v = parseFeed(kGoogleNews, "Google News", "国内");
    ck(v.size() == 3, "parseGoogleNews: 3 items");
    if (v.size() == 3) {
        ck(v[0].source == "Reuters" && v[0].category == "国内", "item0 source+category");
        ck(v[0].title == "美联储维持利率不变, 通胀担忧升温 - Reuters", "item0 title kept (Google News 标题保留源)");
        ck(v[0].link == "https://news.google.com/rss/link/AAA", "item0 link");
        ck(v[1].link == "https://news.google.com/rss/link/BBB", "item1 CDATA link");
        ck(v[1].source == "新浪财经", "item1 source");
        ck(v[2].title == "无来源字段应回退到 feed 源 - AP News", "item2 title no source");
        ck(v[2].epoch > 0, "item2 epoch parsed");
        // 时间序: 07(09:30+0800) > 06(10:00) > 04
        ck(v[1].epoch > v[0].epoch && v[0].epoch > v[2].epoch, "epoch order 07>06>04");
        // 描述去 HTML
        ck(v[0].description == "Fed holds rates as inflation concerns grow", "item0 desc dehtml");
    }

    // ---- parseFeed: Atom 形状 ----
    auto a = parseFeed(kAtom, "CNBC", "国际");
    ck(a.size() == 1 && a[0].link == "https://www.cnbc.com/2025/10/07/rally.html", "atom link href");
    ck(a[0].epoch > 0, "atom published parsed");
    ck(a[0].description == "A modest jobs report boosted sentiment.", "atom summary");

    // ---- dedupItems ----
    {
        std::vector<NewsItem> d = v;
        d.push_back(v[0]);                       // 重复链接 AAA
        d.push_back({"无链接项", "", "x", "国内", "", 0});   // 无链接不合并
        auto u = dedupItems(d);
        ck(u.size() == 4, "dedupItems: 3 unique links + 1 no-link = 4");
    }

    // ---- buildFeedUrl ----
    {
        NewsQuery q; q.keyword = "stock market"; q.lang = "en"; q.region = "US";
        ck(buildFeedUrl(q).rfind("https://news.google.com/rss/search?q=stock", 0) == 0, "buildFeedUrl en");
        NewsQuery z; z.keyword = "股票"; z.lang = "zh-CN"; z.region = "CN";
        ck(buildFeedUrl(z).find("%E8%82%A1%E7%A5%A8") != std::string::npos, "buildFeedUrl zh encoded");
    }

    // ---- SQLite: cache / load / search / 去重 ----
    {
        const char* db = "/tmp/twp_news_test.db";
        remove(db);
        // 首次缓存: 3 条 GoogleNews + 1 条 CNBC
        std::vector<NewsItem> all = v;
        all.insert(all.end(), a.begin(), a.end());
        int n = cacheNews(db, all);
        ck(n == 4, "cacheNews: 4 inserted");

        // 再次缓存同样数据 -> 应新增 0
        std::vector<NewsItem> dup = all;
        ck(cacheNews(db, dup) == 0, "cacheNews: re-cache returns 0");

        // load 全部, 按时间倒序, 共 4 条
        auto load = loadNews(db, "");
        ck(load.size() == 4, "loadNews: 4 total");
        ck(load[0].epoch >= load[1].epoch, "loadNews epoch desc");

        // 按分类过滤
        auto zc = loadNews(db, "国内");
        ck(zc.size() == 3, "loadNews filter 国内: 3");
        auto us = loadNews(db, "国际");
        ck(us.size() == 1 && us[0].source == "CNBC", "loadNews filter 国际: 1");

        // 关键词检索
        auto q1 = searchNews(db, "美联储");
        ck(q1.size() == 1 && q1[0].link.find("AAA") != std::string::npos, "searchNews 美联储");
        // 通配符被屏蔽 (关键词含 % 和 _ 不应报错/越界)
        auto q2 = searchNews(db, "50_%", 10);
        ck(q2.size() == 0, "searchNews wildcard escaped");
        remove(db);
    }

    // ---- 空 feed / 破坏性内容不崩溃 ----
    {
        ck(parseFeed("<html><body>nothing here</body></html>", "X", "").empty(),
           "parseFeed empty on junk");
        auto e = parseFeed("", "X", "");
        ck(e.empty(), "parseFeed empty on empty body");
    }

    printf("\n=== %s (fails=%d) ===\n", g_fail == 0 ? "ALL PASS" : "SOME FAILED", g_fail);
    return g_fail;
}
