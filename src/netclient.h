// netclient.h — 数据更新网络层 (Qt Network)
// 功能: 拉取全市场日K + 名称, 重建 kline_latest.db
// 数据源: 腾讯(https, 与现有库口径一致) → 失败自动降级新浪(http)
// 供 GUI "更新数据" 按钮 与 --update-data 命令行模式共用
#pragma once
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVector>
#include <functional>
#include "engine.h"     // Bar

// 拉全市场日K并重建数据库
//   dbPath   : kline_latest.db 路径 (原库会被替换; 失败时原库保持不动)
//   limit    : >0 时只拉前 N 只 (测试用, 正常传 0 = 全部)
//   progress : 进度回调 (done, total), 可为空
//   err      : 错误输出
// 返回 true=成功
bool updateMarketData(const QString& dbPath, int limit,
                      const std::function<void(int, int)>& progress,
                      QString* err, bool verbose = false);

// 快速更新(增量): 用批量快照接口(400只/请求, 共14个请求)补全市场最新一日K线
// 请求量极低(与普通用户看行情列表同量级), 秒级完成
//   progress : 进度回调 (done, total)
// 返回: 0=失败  1=成功更新  2=已是最新无需更新
int updateIncremental(const QString& dbPath,
                      const std::function<void(int, int)>& progress,
                      QString* err, bool verbose = false);

// 主力资金流 (新浪接口, 单票按需查询, 温柔请求)
struct FundFlow {
    bool ok = false;
    QString err;
    int days = 0;           // 有效天数
    double main5 = 0;       // 主力(超大单)净流入合计(万元)
    double retail5 = 0;     // 散户+中单净流入合计(万元)
    double net5 = 0;        // 总净流入合计(万元)
};
FundFlow fetchFundFlow(const QString& code, int days = 5);

// 资金流网络状态(本会话记忆): 连续失败后自动跳过, 避免离线时反复等待超时
bool fundFlowOffline();
void resetFundFlowOffline();

// 更新估值库 (valuation.db): 东财接口增量拉取全市场 PE(TTM)/PB
//   - 自动从库中最新日期继续, 只拉增量 (每天运行流量极小)
//   - 幂等: 同一日期重复更新不会产生重复数据
//   - limit: >0 只更新前 N 只 (测试用)
bool updateValuationDb(const QString& dbPath, int limit,
                       const std::function<void(int, int)>& progress,
                       QString* err, bool verbose = false);

// 代码格式转换: "002730.SZ" -> "sz002730"
QString toTencentCode(const QString& code);

// 实时行情快照 (腾讯批量接口, 一次请求可查多只)
struct Quote {
    bool ok = false;
    QString code;          // 6 位代码
    QString name;
    double price = 0;      // 现价
    double prev = 0;       // 昨收
    double open = 0, high = 0, low = 0;
    double volume = 0;     // 成交量(手)
    double amount = 0;     // 成交额(库内单位, 亿显示=值/1e6)
    double turnover = 0;   // 换手率 %
    double mktcap = 0;     // 总市值(亿)
    QString date;          // 行情日期 YYYY-MM-DD
    QString time;          // 行情时间 HH:MM:SS (用于判断行情是否新鲜)
};
// 批量拉取实时行情; codes 为 6 位代码列表。失败的代码不会出现在结果里。
QVector<Quote> fetchQuotes(const QStringList& codes);

// ==================== 当日全市场资金流 (东财资金流排行) ====================
// 数据源: 东财资金流排行 (push2 镜像 api/qt/clist/get, fid=f62, fs=沪深京A股)
//   一次请求 100 只, 全市场约 5900 只 = 60 页, 约 20-40 秒(实测 82.push2 镜像 60 页 22 秒零失败;
//   裸域名 push2.eastmoney.com 会被限流, 故代码里默认走镜像 + 失败换域名重试)。口径为"当日":
//     f62 = 主力净流入(元)   f184 = 主力净占比(%)   f66 = 超大单净额
//     f72 = 大单净额         f78  = 中单净额         f84 = 小单净额
//   注: 主力净流入 = 超大单 + 大单; 净占比 = 净额 / 成交额, 光看净额会被大市值股刷屏。
// 落库: fundflow.db 表 fundflow_daily(code,date,name,close,change_pct,main_net,main_pct,
//                                     super_net,big_net,mid_net,small_net, PRIMARY KEY(code,date))
//   金额单位为元; 界面/脚本惯用"亿"/"万元", 自行换算。
//   结构体 FundFlowDay 定义在 engine.h(引擎/策略层共用, 避免循环依赖)

// 6 位代码 -> 带后缀(与 toTencentCode/fromTencentCode 同规则; 北交所 4/8/920)
QString codeWithSuffix(const QString& bare6);

// 更新"当日"全市场资金流并落库 —— 双数据源(一个被限流就换另一个)
//   ① 新浪 MoneyFlow.ssl_bkzj_ssggzj : num=1000/次 -> 全市场约 6 个请求, 十几秒;
//      给 现价/涨跌幅/成交额/主力净额/主力净占比 —— 但没有四档明细
//   ② 东财 push2 api/qt/clist/get   : 每页最多 100 -> 约 60 个请求;
//      给超大单/大单/中单/小单四档明细, 但**容易被按 IP 限流**(限流时直接空回复,
//      换 82./1./7. 等镜像一样, 因为封的是 IP)
//   srcPref: 0=自动(先新浪, 失败再东财)  1=只用新浪  2=只用东财
//   force=false 时, 若库里已有当天数据直接返回 2(不重复打接口)
//   返回: 0=失败 1=成功 2=已是最新
//   outInfo: 可选, 返回一句人类可读的结果说明(用了哪个源/抓到多少只/覆盖率)
int updateFundFlowToday(const QString& dbPath, int srcPref, bool force,
                        const std::function<void(int, int)>& progress,
                        QString* err, QString* outInfo = nullptr, bool verbose = false);

// 读取某日全市场资金流, 按主力净流入降序; date 为空=取库中最新日期
//   limit > 0 只取前 N 条; outDate 可空, 返回实际使用的日期
std::vector<FundFlowDay> loadFundFlow(const QString& dbPath, const QString& date,
                                      int limit, QString* outDate);

// 读某只股票在库中的最新一条资金流(带后缀代码); 无数据时 date 为空
FundFlowDay fundFlowOf(const QString& dbPath, const QString& code);
