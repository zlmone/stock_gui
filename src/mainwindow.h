// mainwindow.h — 主窗口: 左侧策略面板 + 右侧结果表格
#pragma once
#include <QMainWindow>
#include <QString>
#include <QTextEdit>
#include <QSyntaxHighlighter>
#include "engine.h"
#include "backtest.h"
#include "news.h"

class QsciScintilla;   // QScintilla 源码编辑器(仅用指针, 完整头文件在 mainwindow.cpp 中引入)

class QTableWidget;
class QLineEdit;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QVBoxLayout;
class QComboBox;
class QNetworkAccessManager;
void probeFunc(int a, int b);

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 无头自检: 构造UI后自动跑一遍默认池体检并打印 (--selftest)
    void runSelfTest();

protected:
    void closeEvent(class QCloseEvent* e) override;   // 退出时保存窗口尺寸/状态

private slots:
    void runCheckup();
    void runScanAll();
    void loadPool();
    void applyPreset(int presetId);
    void onFiltersChanged();
    void runStrategyBacktest();       // 策略回测: 输入的代码
    void runStrategyScan();           // 策略回测: 全市场扫描排序
    void runFormScan();               // 形态筛选 (on_scan)
    void onStrategyChanged();         // 策略切换: 按能力启用/禁用按钮
    void onBtFiltersChanged();        // 回测筛选条件变化
    void resetStrategyParams();       // 策略参数恢复默认
    void saveStrategyParams();        // 参数工作室: 把当前参数写回 .lua 文件
    void checkStrategySyntax();       // 策略语法预检 (编辑器地基: 保存/运行前拦截)
    bool strategySyntaxOk(const QString& luaFile, QString* errMsg = nullptr);  // 底层语法检查(返回是否通过)
    void reloadSourceIntoEditor();        // 代码工作室: 从磁盘重读当前策略到编辑区
    void saveStrategySource();            // 代码工作室: 把编辑区内容写回 .lua
    void resetStrategySource();           // 代码工作室: 编辑区还原磁盘原文
    void showStockDetail(int row, int col);   // 个股详情弹窗(双击体检表)
    void showStockDetailByCode(const std::string& code);  // 详情弹窗核心(按代码)
    void showSectorStats();                   // 板块聚合统计(当前结果)
    void updateData();                // 联网更新全市场数据
    void showTradeDetail(int row, int col);

    // --- 新闻采集 ---
    void updateNews();                       // 抓取+解析+缓存全部源(联网, 带进度/离线记忆)
    void loadNewsView(const QString& category);  // 按分类从缓存刷新新闻表
    void onNewsRowActivated(int row);        // 双击列表行 -> 打开本地阅读器(离线可读)
    void showNewsReader(const NewsItem& it); // 本地新闻阅读器: 显示已缓存正文, 可在线补抓全文
    void openNewsHtmlLocal(const NewsItem& it); // 把正文渲染成本地 HTML, 用系统浏览器打开(离线可看)
    static QByteArray fetchNewsFeed(QNetworkAccessManager& mgr, const QString& url,
                                    int timeoutMs, QString* err);  // 单 feed 同步拉取

private:
    void buildUI();
    void buildSidePanel(QVBoxLayout* sv);
    AnalyzeParams currentParams() const;
    bool matchFilters(const Stats& s) const;
    void fillTable(const std::vector<Stats>& rows);
    void refreshFromCache();
    void fillBtTable(const std::vector<BacktestResult>& rows);
    void refreshBtTable();            // 按筛选条件从 btResults_ 刷新回测表
    bool matchBtFilters(const BacktestResult& b) const;
    void rebuildParamWidgets();       // 按当前策略重建参数控件
    std::vector<StrategyParam> currentStrategyParams() const;
    void fillScanTable();

    // --- 数据 ---
    class QPushButton* btnUpdate_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLineEdit*    codeEdit_ = nullptr;
    QLabel*       status_ = nullptr;

    // --- 策略参数 ---
    QDoubleSpinBox* dipSpin_ = nullptr;    // 急杀阈值
    QDoubleSpinBox* tpSpin_ = nullptr;     // 反弹目标
    QSpinBox*       holdSpin_ = nullptr;   // 持有窗口
    class QPushButton* btnCheckSyntax_ = nullptr;   // 检查策略语法按钮
    QLabel*       stratCheckLabel_ = nullptr;       // 语法检查结果提示(绿=OK/红=错误)
    class QPushButton* btnSaveParams_ = nullptr;    // 参数工作室: 把参数写回 .lua

    // --- 筛选条件 (勾选 + 数值) ---
    QCheckBox* fFixRateOn_ = nullptr;  QDoubleSpinBox* fFixRateVal_ = nullptr;
    QCheckBox* fSamplesOn_ = nullptr;  QSpinBox*       fSamplesVal_ = nullptr;
    QCheckBox* fBacktestOn_ = nullptr; QDoubleSpinBox* fBacktestVal_ = nullptr;
    QCheckBox* fWorstOn_ = nullptr;    QDoubleSpinBox* fWorstVal_ = nullptr;
    QCheckBox* fZtOn_ = nullptr;       QSpinBox*       fZtVal_ = nullptr;
    QCheckBox* fPosOn_ = nullptr;      QDoubleSpinBox* fPosLo_ = nullptr;  QDoubleSpinBox* fPosHi_ = nullptr;
    QCheckBox* fMa20On_ = nullptr;     QDoubleSpinBox* fMa20Val_ = nullptr;
    QCheckBox* fPbOn_ = nullptr;       QDoubleSpinBox* fPbVal_ = nullptr;
    QCheckBox* fChipProfitOn_ = nullptr; QDoubleSpinBox* fChipProfitVal_ = nullptr;  // 筹码获利盘 ≤
    QCheckBox* fChipConcOn_ = nullptr;   QDoubleSpinBox* fChipConcVal_ = nullptr;    // 筹码集中度(70) ≥
    QCheckBox* fChipConc90On_ = nullptr; QDoubleSpinBox* fChipConc90Val_ = nullptr;  // 筹码集中度(90) ≥

    QCheckBox* onlyMatch_ = nullptr;
    QCheckBox* fundFlowOn_ = nullptr;   // 详情是否查询主力资金流(联网)

    // 全市场扫描
    class QProgressBar* progress_ = nullptr;
    class QPushButton* btnRun_ = nullptr;
    class QPushButton* btnScan_ = nullptr;
    std::vector<Stats> scanCache_;   // 扫描缓存(全部股票), 筛选变化即时过滤
    bool scanMode_ = false;

    // 策略回测
    class QComboBox* stratCombo_ = nullptr;
    class QTabWidget* rightTabs_ = nullptr;
    QTableWidget* btTable_ = nullptr;
    class QPushButton* btnBt1_ = nullptr;
    class QPushButton* btnBt2_ = nullptr;
    std::vector<BacktestResult> btResults_;
    Backtest* bt_ = nullptr;

    // 回测筛选条
    QCheckBox* btF1On_ = nullptr;  QSpinBox*       btF1Val_ = nullptr;   // 交易数 >=
    QCheckBox* btF2On_ = nullptr;  QDoubleSpinBox* btF2Val_ = nullptr;   // 总收益 >= %
    QCheckBox* btF3On_ = nullptr;  QDoubleSpinBox* btF3Val_ = nullptr;   // 胜率 >= %
    QCheckBox* btF4On_ = nullptr;  QDoubleSpinBox* btF4Val_ = nullptr;   // 最大回撤 <= %

    // 形态筛选
    QTableWidget* scanTable_ = nullptr;
    QPushButton* btnBt3_ = nullptr;
    struct ScanHitRow {
        std::string code, name, detail;
        double price = 0;
    };
    std::vector<ScanHitRow> scanHits_;

    // 策略参数 (动态控件)
    class QFormLayout* stratParamLayout_ = nullptr;
    std::vector<std::pair<StrategyParam, class QDoubleSpinBox*>> spWidgets_;

    // 代码工作室 (内联编辑 on_bar/on_scan 源码)
    QsciScintilla*     srcEdit_ = nullptr;             // 源码编辑区 (QScintilla, 带官方 Lua 词法器)
    class QPushButton* btnSaveSource_ = nullptr;       // 保存源码到文件
    class QPushButton* btnReloadSource_ = nullptr;     // 重载参数(重读 params)
    class QPushButton* btnResetSource_ = nullptr;      // 重置源码(还原磁盘原文)

    // News collection tab members
    QTableWidget* newsTable_ = nullptr;
    class QComboBox*    newsFilter_ = nullptr;
    class QPushButton*  btnUpdateNews_ = nullptr;
    class QComboBox*    newsInterval_ = nullptr;
    class QComboBox*    newsOpenMode_ = nullptr;  // 双击打开方式: 程序内 / 本地浏览器 / 网上浏览
    class QGroupBox*    codeStudioBox_ = nullptr;  // 策略代码工作室(作为右侧独立标签页, 便于最大化编辑)
    class QWidget*      filterBox_ = nullptr;      // 筛选条件(可折叠), 挂到"四维体检"页顶部
    class QWidget*      presetBox_ = nullptr;      // 预设策略(一键配置), 挂到"四维体检"页顶部
    class QWidget*      strategyCtlBox_ = nullptr; // 策略选择/参数/回测按钮组, 挂到"策略"标签页
    class QWidget*      settingsBox_ = nullptr;    // 体检口径, 收进"设置"对话框
    class QDialog*      settingsDlg_ = nullptr;    // 设置对话框(低频项)
    class QTimer*       newsTimer_ = nullptr;
    QNetworkAccessManager* newsNM_ = nullptr;

    Engine engine_;
    bool engineReady_ = false;
};
