// ============================================================
// engine.h — 股票四维分析引擎（纯 C++ / 无 GUI 依赖）
// 维度: 行为(修复率/回测/最差单笔) + 位置(均线/区间)
//       + 估值(PB分位) + 归类(行业/概念)
// 数据: kline_latest.db (日K+名称, 必需) + valuation.db (估值分位, 可选)
//       两个文件与 exe 同目录即可, 见 resolveDbPath
// 兼容: Linux g++ / Windows MSVC (VS2019+) 双平台
// ============================================================
#pragma once
#include <string>

// 程序版本号 (窗口标题/诊断输出/说明文档统一对齐)
#define APP_VERSION "2.9"
#include <vector>
#include <map>
#include <utility>
#include <sqlite3.h>

struct Bar {
    std::string date;
    double open = 0, high = 0, low = 0, close = 0, volume = 0;
    double amount = 0;         // 成交额(库内单位, 显示换算: 亿 = amount/1e6)
    double turnover = 0;       // 换手率%(历史数据可能为0)
};

// 快照附加信息 (来自 stocks 表: 市值/换手, 由数据更新维护)
struct SnapInfo {
    double mktcap = 0;         // 总市值(亿)
    double fcap = 0;           // 流通市值(亿)
    double turnover = 0;       // 最新日换手率(%)
};

// 筹码分布 (三角分布法, 近260日近似)
struct ChipDist {
    bool ok = false;
    double profitRatio = 0;    // 获利盘 %
    double cost5 = 0, cost15 = 0, cost50 = 0, cost85 = 0, cost95 = 0;
    double conc70 = 0;         // 70%筹码集中度 %
    double conc90 = 0;         // 90%筹码集中度 %
};

struct Stats {
    std::string code, name;
    bool ok = false;
    double price = 0;          // 最新收盘
    // --- 行为维度 ---
    double fixRate = 0;        // 修复率 % (急杀后5日内弹>=5%比例)
    int    events = 0;         // 急杀样本数
    double backtest = 0;       // 回测复利 %
    double worst = 0;          // 最差单笔 %
    int    ztCount = 0;        // 涨停次数
    // --- 位置维度 ---
    double ma5 = 0, ma10 = 0, ma20 = 0, ma60 = 0;
    double pos60 = 0;          // 60日区间位置 % (0=最低,100=最高)
    double vsMa20 = 0;         // 相对MA20 %
    // --- 估值维度 ---
    double pbCurrent = 0;      // 当前PB (外部注入)
    double pbPct3y = -1;       // 近3年PB分位 % (-1=无数据)
    // --- 归类 ---
    std::string industry;      // 行业
    std::string concepts;      // 概念(;分隔)
    // --- 扩展指标 ---
    double volRatio = 0;       // 量比(最新日量/前5日均量)
    double turnover = 0;       // 换手率%(最新日, 来自快照)
    double ret5 = 0, ret20 = 0;    // 5日/20日涨幅 %
    double amp = 0;            // 最新日振幅 %
    double fromHigh60 = 0;     // 距60日高点 % (负值)
    double amount5 = 0;        // 近5日均成交额(亿)
    double mktcap = 0;         // 总市值(亿, 来自快照)
    int    emaRedDays = 0;     // 动能: 连续红柱天数(0=当前黄)
    double chipProfit = -1;    // 筹码获利盘 % (-1=未计算)
    double chipConc = -1;      // 70%筹码集中度 % (越高越集中)
    double chipConc90 = -1;    // 90%筹码集中度 %
    double chipCost50 = 0;     // 筹码平均成本

    // --- 状态判定 ---
    std::string verdict;       // 低位观察/中位等触发/偏强/高位/下跌趋势
};

// 策略参数: 控制"急杀反弹"行为统计口径 (默认值 = 此前所有分析的基准口径)
struct AnalyzeParams {
    double dipThreshold = -7.0;   // 急杀判定: 单日跌幅 <= 该值(%), 默认 -7
    double dipFloor     = -11.5;  // 急杀判定下限(排除除权跳变/超大跌幅), 默认 -11.5
    double takeProfit   = 5.0;    // 止盈目标(%), 默认 +5
    int    holdDays     = 5;      // 最长持有交易日, 默认 5
};

class Engine {
public:
    // latestDb: kline_latest.db 路径; histDb: kline_full_history.db 路径
    Engine(const std::string& latestDb, const std::string& histDb);
    ~Engine();

    // 打开数据库, 返回是否成功
    bool open(std::string& err);

    // 估值库(kline_full_history/valuation.db)是否可用; 缺失时估值分位返回 -1
    bool hasHist() const { return dbHist_ != nullptr; }

    // 惰性连接估值库: 未打开时尝试打开(启动瞬间被占用/锁定后, 体检时会自动重连)
    bool ensureHistDb();

    // 诊断: 估值库路径与最近一次错误信息
    std::string histPath() const { return histDb_; }
    std::string histOpenErr() const { return histOpenErr_; }

    // 关闭连接 (数据更新前释放文件句柄; 之后可重新 open())
    void close();

    // 全市场最新交易日 (停牌/退市股过滤用)
    std::string latestMarketDate();

    // 拼接K线序列: 全历史库(截止某日) + 最新库(近期窗口), 返回按日期升序
    std::vector<Bar> loadSeries(const std::string& code, int maxBars);

    // 核心: 计算四维指标 (pbCurrent 由外部注入, 可传 0)
    Stats analyze(const std::string& code, const std::string& name, double pbCurrent = 0,
                  const AnalyzeParams& params = AnalyzeParams());

    // 估值: 近3年PB分位 (数据源 valuation.db 或全历史库估值表)
    double pbPercentile3y(const std::string& code, double currentPb);

    // 估值库中该股票最新一条 PB (无数据返回 0)
    double latestPb(const std::string& code);

    // 名称查询 (kline_latest.db/stocks)
    std::string stockName(const std::string& code);

    // 全部股票 (code, name) 列表 —— 全市场扫描用
    std::vector<std::pair<std::string, std::string>> allStocks();

    // 行业/概念加载 (CSV, 可选)
    bool loadMaps(const std::string& industryCsv, const std::string& conceptCsv);

    // 行业/概念查询: "000509.SZ" 或 "000509" -> 名称 (无数据返回空串)
    std::string industryOf(const std::string& code);
    std::string conceptsOf(const std::string& code);

    // 估值库中该股票最新 PE_TTM (无数据返回 0)
    double latestPe(const std::string& code);

    // 近3年 PE 分位 (数据源同 PB)
    double pePercentile3y(const std::string& code, double currentPe);

    // 筹码分布 (三角分布法; bars 建议传入近 260 根前复权数据)
    ChipDist chipDist(const std::vector<Bar>& bars, double price, int buckets = 120);

    // 快照附加信息(市值/换手) 缓存: 打开主库后自动加载
    void loadSnapInfo();

private:
    sqlite3* dbLatest_ = nullptr;
    sqlite3* dbHist_ = nullptr;
    void detectValTables();                 // 探测估值表名(valuation/valuation_daily)
    std::string histOpenErr_;               // 最近一次估值库打开失败原因
    std::string latestDb_, histDb_;
    std::string valTable_ = "valuation";   // 估值表名 (兼容 valuation_daily)
    std::string valDateCol_ = "d";          // 表内日期列名
    bool hasHistKline_ = false;             // 估值库是否含K线表(打包版不含, 跳过历史查询提速)
    std::string latestDateCache_;           // 最新交易日缓存(避免重复全表扫描)
    std::map<std::string, SnapInfo> snapInfo_;   // code -> 市值/换手(来自stocks表)
    std::map<std::string, std::string> industry_, concepts_;   // code6 -> 值
};

// 工具: 归一化日期 '2026-09-17' -> '20260917'
std::string normDate(const std::string& d);

// ---------- 数据库路径解析 ----------
// 解析顺序: ① 环境变量 STOCK_DB_DIR  ② exe 同目录  ③ /home/srdev/trade (开发机兜底)
std::string exeDir();
bool fileExists(const std::string& path);
std::string resolveDbPath(const std::string& filename);
