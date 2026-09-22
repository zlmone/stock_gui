// ============================================================
// pool.h — "默认池"持久化 (pool.db)
//   * 原先默认池是写死在代码里的一串代码(loadPool 里 setText 19 只), 改了要重新编译;
//     现在存进 exe 同目录(或 STOCK_DB_DIR)的 pool.db, 界面里随时增删。
//   * 为什么单独一个库: kline_latest.db 在"全量重建/全市场更新"时会被整库替换掉,
//     自选池若放进去会被一起清掉 —— 所以像 valuation.db / fundflow.db 一样独立存放。
//   * 换新版解压到新目录时 pool.db 不会自动跟过来: 想长期保留就把 pool.db
//     (连同 kline_latest.db) 设成 STOCK_DB_DIR 指向的固定目录, 或在新版里用
//     "池管理 → 导出/导入"搬一次。
// ============================================================
#pragma once
#include <QString>
#include <QStringList>
#include <vector>
#include <utility>

// 池中一条记录
struct PoolItem {
    QString code;       // 带后缀, 例 "600000.SH" (与库内K线口径一致)
    QString name;       // 名称(可能为空 —— 采集时手上没有名称)
    QString source;     // 来源备注: 手动/体检结果/形态筛选/资金流/内置种子 ...
    QString addedAt;    // 加入时间 YYYY-MM-DD HH:MM:SS
};

// pool.db 路径 (resolveDbPath: STOCK_DB_DIR > exe 同目录)
QString poolDbPath();

// 规范化代码: "600519" -> "600519.SH"; 已带后缀的原样返回(统一大写)
//   规则与 netclient::codeWithSuffix 一致(6/9/5->SH, 4/8/920->BJ, 其余 SZ)
QString poolNormalize(const QString& raw);
// 规范化一串(逗号/空格/换行分隔), 去重保序
QStringList poolNormalizeList(const QString& raw);

// 读全部(按加入顺序)
std::vector<PoolItem> poolLoad(const QString& dbPath);
// 池内条数 (-1 = 打不开库)
int poolCount(const QString& dbPath);
// 追加(已存在的只更新名称/来源, 不改变原加入时间); 返回新增条数, -1 = 失败
int poolAdd(const QString& dbPath, const std::vector<std::pair<QString, QString>>& codeName,
            const QString& source, QString* err);
// 移除; 返回真正删掉的条数, -1 = 失败
int poolRemove(const QString& dbPath, const QStringList& codes, QString* err);
// 清空; 返回删掉的条数, -1 = 失败
int poolClear(const QString& dbPath, QString* err);
// 是否已在池中
bool poolContains(const QString& dbPath, const QString& code);

// 首次运行(池为空)时的初始种子 —— 只用于"把老内置列表搬进库", 之后不再参与任何逻辑
QStringList poolSeedCodes();
