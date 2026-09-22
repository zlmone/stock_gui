// news.h — 财经新闻采集核心 (纯逻辑, 无 Qt / 无网络, 便于单元测试)
// 职责: 解析 RSS2.0 / Atom / GoogleNews / 国内 JSON 源, 抽正文, 按 link 去重, SQLite 缓存
// GUI 层 (mainwindow) 负责网络拉取 + 显示; 本文件只处理 "字节 -> 条目 -> 库"
#pragma once
#include <string>
#include <vector>

struct NewsItem {
    std::string title;      // 标题
    std::string link;       // 原文链接 (去重的键)
    std::string source;     // 来源名 (来自 feed, 不含 Google News 标题里的 "- 源")
    std::string category;   // 分类: 国内 / 国际 / 宏观 / 个股
    std::string description;// 摘要 (可能含 HTML)
    std::string content;    // 正文/快讯全文 (离线阅读用); 来自 feed 字段或抓取的详情页
    long long epoch = 0;    // 发布时间的 unix 秒, 0=未知 (排序时放末尾)
};

// 一个采集任务
struct NewsQuery {
    std::string keyword;   // 检索词, 如 "股票"/"stock market"/个股名 (url 为空时使用)
    std::string source;    // 该任务的来源名 (显示用, 如 "CNBC"/"MarketWatch"; 空=由 GUI 填)
    std::string category;  // 分类标签: 国内/国际/宏观/个股
    std::string lang;      // 界面语言: zh-CN / en
    std::string region;    // 地区: CN / US
    std::string url;       // 固定 feed URL (优先); 空=用 keyword 构造 GoogleNews 查询
};

// 构造一份 Google News RSS 检索 URL (对 keyword 做 URL 编码)
std::string buildFeedUrl(const NewsQuery& q);

// 默认采集任务: 国内财经 + 国际财经 + 宏观 (固定源兜底, 失败自动跳过)
std::vector<NewsQuery> defaultQueries();

// 解析一份 feed 的原始 XML (兼容 RSS2.0 / Atom / GoogleNews)
//   source  : 该 feed 的来源名 (如 "Google News"/"CNBC"), 覆盖条目未自带的情况
//   category: 该 feed 分组的分类标签 (可为空)
std::vector<NewsItem> parseFeed(const std::string& body, const std::string& source,
                                const std::string& category = "");

// 解析一份 JSON 格式的新闻列表 (财联社 / 华尔街见闻 / 东方财富 等国内源)
//   与 parseFeed 同签名; 返回空=解析失败或无条目 (调用层自动跳过该源)
std::vector<NewsItem> parseJsonFeed(const std::string& body, const std::string& source,
                                    const std::string& category = "");

// 自动判断 JSON({) 或 XML(<) 并分发解析 (GUI 层统一调用)
std::vector<NewsItem> collectItems(const std::string& body, const std::string& source,
                                   const std::string& category = "");

// 按 link 去重 (同一链接保留发布时间最早的那条), 保持输入顺序
std::vector<NewsItem> dedupItems(std::vector<NewsItem> items);

// SQLite 存储
//   cacheNews: 仅插入尚未缓存的链接, 返回本次新增条数 (重复/失败不计)
int cacheNews(const std::string& dbPath, std::vector<NewsItem>& items);
//   loadNews:  读取缓存用于显示; categoryFilter 空=全部, 否则按分类过滤, 按时间倒序
std::vector<NewsItem> loadNews(const std::string& dbPath, const std::string& categoryFilter = "");
//   searchNews: 在缓存中按关键词检索标题/摘要/来源/分类 (用于"个股相关推荐")
std::vector<NewsItem> searchNews(const std::string& dbPath, const std::string& keyword, int limit = 30);
//   updateNewsContent: 把抓取到的正文写回指定 link 的条目 (仅当比已有正文更长时覆盖), 返回是否更新
int updateNewsContent(const std::string& dbPath, const std::string& link, const std::string& content);

//   loadNewsItem: 按 link 读取单条(含正文), 供本地阅读器使用
NewsItem loadNewsItem(const std::string& dbPath, const std::string& link);

// 从文章 HTML 中尽力抽取正文纯文本 (去脚本/样式/标签, 按 <p> 收集段落)。
// 无法抽取(如需要 JS 渲染的页面)时返回空串 —— 调用方应保留原有正文。
std::string extractArticleText(const std::string& html);
