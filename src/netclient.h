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
