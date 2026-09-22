// mainwindow.cpp — 主窗口: 左侧策略面板 + 右侧结果表格
#include "mainwindow.h"
#include "klinechart.h"
#include "netclient.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QDesktopServices>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QAbstractItemView>

static int g_newsFailures = 0;
static bool newsOffline() { return g_newsFailures >= 3; }
#include <cstdio>
#include <QTableWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTextEdit>
#include <QSyntaxHighlighter>
#include <fstream>
#include <sstream>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QStringList>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QSplitter>
#include <QRegularExpression>
#include <QFont>
#include <QColor>
#include <QProgressBar>
#include <QApplication>
#include <QElapsedTimer>
#include <QComboBox>
#include <QTabWidget>
#include <QDialog>
#include <QTextBrowser>
#include <QTreeWidget>
#include <algorithm>
#include <map>
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QDate>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexerlua.h>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QKeySequence>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QItemSelectionModel>

// 带数值的表格项: 点表头排序时按数值比较(而非字符串), 如 "+20.3%" vs "-2.4%"
class NumericItem : public QTableWidgetItem {
public:
    NumericItem(const QString& text, double value)
        : QTableWidgetItem(text), val_(value) {}
    bool operator<(const QTableWidgetItem& other) const override {
        const NumericItem* o = dynamic_cast<const NumericItem*>(&other);
        if (o) return val_ < o->val_;
        return QTableWidgetItem::operator<(other);
    }
private:
    double val_;
};

// 智能代码补全: "002385" -> "002385.SZ", "600519" -> "600519.SH"
static std::string normalizeCode(const QString& q) {
    QString c = q.trimmed().toUpper();          // 统一大写, 容忍小写后缀
    if (c.isEmpty()) return std::string();
    // 支持 sz002730 / SH600519 / bj920002 前缀格式
    if (c.startsWith("SZ") || c.startsWith("SH") || c.startsWith("BJ")) {
        QString pure = c.mid(2);
        if (pure.length() == 6) return (pure + "." + c.left(2)).toStdString();
    }
    if (c.contains('.')) return c.toStdString();  // 已含后缀
    if (c.length() == 6) {
        QChar d = c.at(0);
        if (d == '6' || d == '5' || d == '9') return (c + ".SH").toStdString();
        if (d == '4' || d == '8') return (c + ".BJ").toStdString();
        return (c + ".SZ").toStdString();
    }
    return c.toStdString();
}

// 停牌/退市过滤: 最后K线距全市场最新交易日超过 maxDays 天 -> 视为失效
static bool isStaleStock(const std::string& lastDate, const std::string& latestDate, int maxDays = 15) {
    if (lastDate.empty() || latestDate.empty()) return false;
    QDate a = QDate::fromString(QString::fromStdString(lastDate), QStringLiteral("yyyy-MM-dd"));
    QDate b = QDate::fromString(QString::fromStdString(latestDate), QStringLiteral("yyyy-MM-dd"));
    if (!a.isValid() || !b.isValid()) return false;
    return a.daysTo(b) > maxDays;
}

// 读取一份策略 .lua 文件到字符串 (供代码工作室预填)
static std::string readStrategyFile(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// ===== 代码工作室: 简易 Lua 语法高亮器 (单块遍历, 避免注释内再高亮字符串) =====
// 可折叠分组: 点标题行的箭头展开/收起内容。
// (Qt 自带的 checkable QGroupBox 只是"禁用"里面的子控件, 并不会收起来, 所以自写一个)
class CollapsibleBox : public QWidget {
public:
    explicit CollapsibleBox(const QString& title, bool expanded = true, QWidget* parent = nullptr)
        : QWidget(parent) {
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);

        head_ = new QToolButton(this);
        head_->setText(title);
        head_->setCheckable(true);
        head_->setChecked(expanded);
        head_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        head_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        head_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        head_->setCursor(Qt::PointingHandCursor);
        head_->setStyleSheet(QStringLiteral(
            "QToolButton{border:none;font-weight:bold;padding:4px 2px;text-align:left;}"));
        v->addWidget(head_);

        body_ = new QWidget(this);
        contentLay_ = new QVBoxLayout(body_);
        contentLay_->setContentsMargins(12, 2, 2, 6);
        v->addWidget(body_);
        body_->setVisible(expanded);

        connect(head_, &QToolButton::toggled, this, [this](bool on) {
            body_->setVisible(on);
            head_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        });
    }
    QVBoxLayout* content() const { return contentLay_; }

private:
    QToolButton* head_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* contentLay_ = nullptr;
};

class LuaHighlighter : public QSyntaxHighlighter {
public:
    explicit LuaHighlighter(QTextEdit* parent) : QSyntaxHighlighter(parent) {
        kw_ << "and" << "break" << "do" << "else" << "elseif" << "end"
            << "false" << "for" << "function" << "if" << "in" << "local"
            << "nil" << "not" << "or" << "repeat" << "return" << "then"
            << "true" << "until" << "while" << "goto";
        QTextCharFormat kwFmt;
        kwFmt.setForeground(QColor(0x00, 0x00, 0xcc));
        kwFmt.setFontWeight(QFont::Bold);
        kwFmt_ = kwFmt;
    }
protected:
    void highlightBlock(const QString& text) override {
        int n = (int)text.length();
        int i = 0;
        while (i < n) {
            QChar ch = text.at(i);
            if (ch == QChar('-') && i + 1 < n && text.at(i + 1) == QChar('-')) {
                int end = text.indexOf(QChar('\n'), i);
                if (end < 0) end = n;
                QTextCharFormat f;
                f.setForeground(QColor(0x2e, 0x7d, 0x32));
                f.setFontItalic(true);
                setFormat(i, end - i, f);
                i = end;
                continue;
            }
            if (ch == QChar('"') || ch == QChar('\'')) {
                int end = text.indexOf(ch, i + 1);
                if (end < 0) end = n; else end++;
                QTextCharFormat f;
                f.setForeground(QColor(0xc6, 0x28, 0x28));
                setFormat(i, end - i, f);
                i = end;
                continue;
            }
            if (ch.isNumber()) {
                int j = i + 1;
                while (j < n && (text.at(j).isNumber() || text.at(j) == QChar('.')
                                 || text.at(j) == QChar('e') || text.at(j) == QChar('E')
                                 || text.at(j) == QChar('+') || text.at(j) == QChar('-'))) j++;
                QTextCharFormat f;
                f.setForeground(QColor(0x88, 0x44, 0xcc));
                setFormat(i, j - i, f);
                i = j;
                continue;
            }
            if (ch.isLetter() || ch == QChar('_')) {
                int j = i + 1;
                while (j < n && (text.at(j).isLetterOrNumber() || text.at(j) == QChar('_'))) j++;
                QString word = text.mid(i, j - i);
                QTextCharFormat f;
                if (kw_.contains(word)) { f = kwFmt_; }
                else { f.setForeground(QColor(0x00, 0x00, 0x00)); }
                setFormat(i, j - i, f);
                i = j;
                continue;
            }
            i++;
        }
    }
private:
    QStringList kw_;
    QTextCharFormat kwFmt_;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), engine_(resolveDbPath("kline_latest.db"), resolveDbPath("valuation.db")) {
    bt_ = new Backtest(resolveStrategiesDir());
    setWindowTitle(QStringLiteral("股票四维"));

    // 窗口尺寸: 优先恢复上次记忆; 首次运行自适应屏幕(94%可用区)
    QSettings st(QStringLiteral("stock_gui"), QStringLiteral("StockGui"));
    QByteArray geo = st.value(QStringLiteral("geometry")).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    } else {
        QScreen* scr = QGuiApplication::primaryScreen();
        QRect av = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 800);
        resize(qMin(1500, (int)(av.width() * 0.94)),
               qMin(940, (int)(av.height() * 0.94)));
    }
    setMinimumSize(1000, 620);
    if (st.value(QStringLiteral("maximized")).toBool())
        showMaximized();

    buildUI();

    // 初始化引擎（数据库与 exe 同目录即可自动发现）
    std::string err;
    engineReady_ = engine_.open(err);
    engine_.loadMaps(exeDir() + "/industry_map.csv", exeDir() + "/concept_map.csv");
    if (engineReady_) {
        bool histOk = engine_.ensureHistDb();   // 惰性重连(防启动瞬间占用)
        if (histOk) {
            status_->setText(QStringLiteral("引擎就绪: 主库已连接 + 估值库已加载"));
        } else {
            status_->setText(QStringLiteral("引擎就绪: 主库已连接 | 估值库未加载: %1")
                .arg(QString::fromStdString(engine_.histPath())));
            status_->setToolTip(QStringLiteral("请确认该路径下存在 valuation.db; 体检/回测时会自动重试连接"));
        }
    } else {
        status_->setText(QString::fromStdString("引擎失败: " + err));
    }
    onStrategyChanged();   // 按默认策略的能力初始化按钮状态
}

void MainWindow::closeEvent(QCloseEvent* e) {
    QSettings st(QStringLiteral("stock_gui"), QStringLiteral("StockGui"));
    st.setValue(QStringLiteral("geometry"), saveGeometry());
    st.setValue(QStringLiteral("maximized"), isMaximized());
    // 一并记住界面状态, 下次打开接着上次的样子
    if (splitter_) st.setValue(QStringLiteral("ui/splitter"), splitter_->saveState());
    if (rightTabs_) st.setValue(QStringLiteral("ui/lastTab"), rightTabs_->currentIndex());
    if (codeEdit_) st.setValue(QStringLiteral("ui/codes"), codeEdit_->text());
    QMainWindow::closeEvent(e);
}

// ==================== UI 构建 ====================
void MainWindow::buildUI() {
    // ---- 菜单栏: 把命令从左侧栏解放出来, 并统一快捷键 ----
    //   标签页 Ctrl+1..5; 主操作 F5/F6/F7
    {
        auto* mData = menuBar()->addMenu(QStringLiteral("数据(&D)"));
        auto* aUpd = mData->addAction(QStringLiteral("更新数据 (快速, 约15秒)"));
        aUpd->setShortcut(QKeySequence(QStringLiteral("Ctrl+U")));
        connect(aUpd, &QAction::triggered, this, &MainWindow::updateData);
        auto* aNews = mData->addAction(QStringLiteral("更新新闻"));
        aNews->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
        connect(aNews, &QAction::triggered, this, &MainWindow::updateNews);
        mData->addSeparator();
        auto* aSet = mData->addAction(QStringLiteral("设置…"));
        aSet->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
        connect(aSet, &QAction::triggered, this, [this]() {
            if (settingsDlg_) { settingsDlg_->show(); settingsDlg_->raise(); settingsDlg_->activateWindow(); }
        });
        mData->addSeparator();
        auto* aQuit = mData->addAction(QStringLiteral("退出"));
        aQuit->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
        connect(aQuit, &QAction::triggered, this, &QWidget::close);

        auto* mRun = menuBar()->addMenu(QStringLiteral("运行(&R)"));
        auto* aChk = mRun->addAction(QStringLiteral("开始体检 (输入的代码)"));
        aChk->setShortcut(QKeySequence(QStringLiteral("F5")));
        connect(aChk, &QAction::triggered, this, &MainWindow::runCheckup);
        auto* aScan = mRun->addAction(QStringLiteral("全市场扫描 + 筛选"));
        aScan->setShortcut(QKeySequence(QStringLiteral("F6")));
        connect(aScan, &QAction::triggered, this, &MainWindow::runScanAll);
        auto* aForm = mRun->addAction(QStringLiteral("形态筛选 (全市场选股)"));
        aForm->setShortcut(QKeySequence(QStringLiteral("F7")));
        connect(aForm, &QAction::triggered, this, &MainWindow::runFormScan);
        mRun->addSeparator();
        auto* aSector = mRun->addAction(QStringLiteral("板块聚合统计 (当前结果)"));
        connect(aSector, &QAction::triggered, this, &MainWindow::showSectorStats);

        auto* mView = menuBar()->addMenu(QStringLiteral("视图(&V)"));
        // 与右侧实际标签页一一对应(3 个: 四维体检 / 策略 / 新闻资讯)
        const QString tabNames[4] = { QStringLiteral("四维体检"), QStringLiteral("策略"),
                                      QStringLiteral("新闻资讯"), QStringLiteral("实时盯盘") };
        for (int i = 0; i < 4; ++i) {
            auto* a = mView->addAction(tabNames[i]);
            a->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
            connect(a, &QAction::triggered, this, [this, i]() {
                if (rightTabs_ && i < rightTabs_->count()) rightTabs_->setCurrentIndex(i);
            });
        }
        mView->addSeparator();
        auto* aScanView = mView->addAction(QStringLiteral("形态筛选视图"));
        aScanView->setShortcut(QKeySequence(QStringLiteral("Ctrl+5")));
        connect(aScanView, &QAction::triggered, this, [this]() {
            if (resultStack_) resultStack_->setCurrentIndex(1);
            if (btnViewScan_) btnViewScan_->setChecked(true);
            if (rightTabs_) rightTabs_->setCurrentIndex(0);
        });

        auto* mHelp = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
        auto* aAbout = mHelp->addAction(QStringLiteral("关于"));
        connect(aAbout, &QAction::triggered, this, [this]() {
            QMessageBox::about(this, QStringLiteral("关于 — 股票四维"),
                QStringLiteral("<b>股票四维</b><br>本地数据库 + 网络数据的股票分析桌面工具<br><br>"
                               "四维 = 行为(修复率/回测) + 位置(均线/区间) + 估值(PB分位) + 归类(行业/概念)"));
        });
    }

    auto* central = new QWidget(this);
    auto* mainLay = new QHBoxLayout(central);
    mainLay->setContentsMargins(6, 6, 6, 6);

    // ---- 左侧: 顶部固定的主操作条 + 下方可滚动参数区 ----
    //   主操作常驻顶部, 不再随参数一起滚走(原先要滚到底才能点到"开始体检")
    auto* leftBox = new QWidget();
    auto* leftLay = new QVBoxLayout(leftBox);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(6);
    {
        auto* bar = new QWidget();
        auto* bl = new QHBoxLayout(bar);
        bl->setContentsMargins(4, 4, 4, 0);
        bl->setSpacing(6);

        btnUpdate_ = new QPushButton(QStringLiteral("⟳ 更新数据"));
        btnUpdate_->setMinimumHeight(30);
        btnUpdate_->setToolTip(QStringLiteral("批量快照补全市场最新一日(温柔模式, 约 15 秒); 如有历史缺口会询问全量重建"));
        connect(btnUpdate_, &QPushButton::clicked, this, &MainWindow::updateData);

        btnRun_ = new QPushButton(QStringLiteral("开始体检"));
        btnRun_->setMinimumHeight(30);
        btnRun_->setToolTip(QStringLiteral("对【股票代码】里的股票做四维体检 (F5)"));
        connect(btnRun_, &QPushButton::clicked, this, &MainWindow::runCheckup);

        btnScan_ = new QPushButton(QStringLiteral("全市场扫描"));
        btnScan_->setMinimumHeight(30);
        {
            QFont bf = btnScan_->font(); bf.setBold(true);
            btnScan_->setFont(bf);
        }
        btnScan_->setToolTip(QStringLiteral("扫描全市场, 并按【四维体检】页顶部的筛选条件选出候选 (F6)"));
        connect(btnScan_, &QPushButton::clicked, this, &MainWindow::runScanAll);

        bl->addWidget(btnUpdate_);
        bl->addWidget(btnRun_);
        bl->addWidget(btnScan_, 1);
        leftLay->addWidget(bar);
    }

    auto* side = new QWidget();
    auto* sv = new QVBoxLayout(side);
    sv->setContentsMargins(4, 4, 4, 4);
    buildSidePanel(sv);          // 参数区(可滚动)
    sv->addStretch();

    auto* scroll = new QScrollArea();
    scroll->setWidget(side);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftLay->addWidget(scroll, 1);

    // ---- 设置对话框: 收纳低频项(体检口径 / 数据选项), 从 菜单 数据 → 设置… 打开 ----
    {
        settingsDlg_ = new QDialog(this);
        settingsDlg_->setWindowTitle(QStringLiteral("设置"));
        settingsDlg_->resize(480, 360);
        auto* sl = new QVBoxLayout(settingsDlg_);
        if (settingsBox_) sl->addWidget(settingsBox_);
        if (fundFlowOn_) sl->addWidget(fundFlowOn_);
        sl->addStretch();
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(bb, &QDialogButtonBox::rejected, settingsDlg_, &QDialog::accept);
        sl->addWidget(bb);
    }

    // ---- 右侧结果区 ----
    auto* right = new QWidget();
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);

    rightTabs_ = new QTabWidget();

    table_ = new QTableWidget();
    QStringList headers = {
        QStringLiteral("代码"), QStringLiteral("名称"), QStringLiteral("现价"),
        QStringLiteral("修复率"), QStringLiteral("样本"), QStringLiteral("回测"),
        QStringLiteral("最差"), QStringLiteral("涨停"), QStringLiteral("60位"),
        QStringLiteral("vsMA20"),
        QStringLiteral("量比"), QStringLiteral("换手%"), QStringLiteral("5日%"),
        QStringLiteral("20日%"), QStringLiteral("振幅%"), QStringLiteral("距60高%"),
        QStringLiteral("额亿"), QStringLiteral("市值亿"),
        QStringLiteral("获利盘%"), QStringLiteral("集中70"), QStringLiteral("集中90"),
        QStringLiteral("PB分位"), QStringLiteral("命中"),
        QStringLiteral("状态")
    };
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);   // 供"用选中行"多选
    connect(table_, &QTableWidget::cellDoubleClicked, this, &MainWindow::showStockDetail);
    // (「四维体检」页在 scanTable_ 建好之后再组装: 它要把形态筛选表也纳进来, 见下面)

    // 回测结果表
    btTable_ = new QTableWidget();
    QStringList bh = {
        QStringLiteral("代码"), QStringLiteral("名称"), QStringLiteral("策略"),
        QStringLiteral("交易数"), QStringLiteral("胜率%"), QStringLiteral("总收益%"),
        QStringLiteral("最差单笔%"), QStringLiteral("最大回撤%"), QStringLiteral("均持天数"),
        QStringLiteral("备注")
    };
    btTable_->setColumnCount(bh.size());
    btTable_->setHorizontalHeaderLabels(bh);
    btTable_->horizontalHeader()->setStretchLastSection(true);
    btTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    btTable_->setAlternatingRowColors(true);
    btTable_->verticalHeader()->setVisible(false);
    connect(btTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::showTradeDetail);

    // 策略回测页 = 过滤条 + 表格
    auto* btPage = new QWidget();
    auto* btl = new QVBoxLayout(btPage);
    btl->setContentsMargins(3, 3, 3, 3);
    {
        auto* fb = new QHBoxLayout();
        fb->addWidget(new QLabel(QStringLiteral("结果筛选:")));
        btF1On_ = new QCheckBox(QStringLiteral("交易数 ≥"));
        btF1On_->setChecked(true);                       // 默认过滤掉无信号的票
        btF1Val_ = new QSpinBox();
        btF1Val_->setRange(1, 200); btF1Val_->setValue(1);
        fb->addWidget(btF1On_); fb->addWidget(btF1Val_);
        btF2On_ = new QCheckBox(QStringLiteral("总收益 ≥"));
        btF2Val_ = new QDoubleSpinBox();
        btF2Val_->setRange(-100, 2000); btF2Val_->setValue(0); btF2Val_->setSuffix(QStringLiteral(" %"));
        fb->addWidget(btF2On_); fb->addWidget(btF2Val_);
        btF3On_ = new QCheckBox(QStringLiteral("胜率 ≥"));
        btF3Val_ = new QDoubleSpinBox();
        btF3Val_->setRange(0, 100); btF3Val_->setValue(60); btF3Val_->setSuffix(QStringLiteral(" %"));
        fb->addWidget(btF3On_); fb->addWidget(btF3Val_);
        btF4On_ = new QCheckBox(QStringLiteral("最大回撤 ≤"));
        btF4Val_ = new QDoubleSpinBox();
        btF4Val_->setRange(1, 100); btF4Val_->setValue(20); btF4Val_->setSuffix(QStringLiteral(" %"));
        fb->addWidget(btF4On_); fb->addWidget(btF4Val_);
        fb->addStretch();
        btl->addLayout(fb);
        btl->addWidget(btTable_, 1);
    }
    // ---- 「策略」页: 策略选择/参数 + 回测结果 + 代码编辑器 ----
    //   原先这三样分散在"左栏 / 策略回测标签 / 策略代码标签", 现在集中到一页
    {
        auto* stratPage = new QWidget();
        auto* spl = new QVBoxLayout(stratPage);
        spl->setContentsMargins(3, 3, 3, 3);
        spl->setSpacing(6);
        if (strategyCtlBox_) spl->addWidget(strategyCtlBox_);   // 选策略 / 调参 / 语法检查 / 回测按钮

        auto* vsplit = new QSplitter(Qt::Vertical);
        vsplit->addWidget(btPage);                             // 回测结果(含结果筛选条)
        if (codeStudioBox_) vsplit->addWidget(codeStudioBox_); // .lua 代码编辑器
        vsplit->setStretchFactor(0, 1);
        vsplit->setStretchFactor(1, 1);
        vsplit->setSizes({ 420, 380 });
        spl->addWidget(vsplit, 1);
        rightTabs_->addTab(stratPage, QStringLiteral("策略"));
    }

    // 过滤条件变化 → 即时刷新
    connect(btF1On_, &QCheckBox::toggled, this, &MainWindow::onBtFiltersChanged);
    connect(btF2On_, &QCheckBox::toggled, this, &MainWindow::onBtFiltersChanged);
    connect(btF3On_, &QCheckBox::toggled, this, &MainWindow::onBtFiltersChanged);
    connect(btF4On_, &QCheckBox::toggled, this, &MainWindow::onBtFiltersChanged);
    connect(btF1Val_, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onBtFiltersChanged);
    connect(btF2Val_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onBtFiltersChanged);
    connect(btF3Val_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onBtFiltersChanged);
    connect(btF4Val_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onBtFiltersChanged);

    // 形态筛选结果表
    scanTable_ = new QTableWidget();
    QStringList sh = {
        QStringLiteral("代码"), QStringLiteral("名称"), QStringLiteral("现价"),
        QStringLiteral("形态备注")
    };
    scanTable_->setColumnCount(sh.size());
    scanTable_->setHorizontalHeaderLabels(sh);
    scanTable_->horizontalHeader()->setStretchLastSection(true);
    scanTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    scanTable_->setAlternatingRowColors(true);
    scanTable_->verticalHeader()->setVisible(false);
    // ---- 「四维体检」页: 顶部一行切换两个结果视图(个股体检 / 形态筛选) ----
    //   原"形态筛选"是独立顶层标签, 现并入本页; 顶层标签由 5 个减到 3 个
    {
        auto* checkPage = new QWidget();
        auto* cpl = new QVBoxLayout(checkPage);
        cpl->setContentsMargins(0, 0, 0, 0);
        cpl->setSpacing(4);

        auto* vrow = new QHBoxLayout();
        btnViewCheck_ = new QPushButton(QStringLiteral("个股体检"));
        btnViewScan_  = new QPushButton(QStringLiteral("形态筛选"));
        btnViewCheck_->setCheckable(true);
        btnViewScan_->setCheckable(true);
        btnViewCheck_->setChecked(true);
        btnViewCheck_->setToolTip(QStringLiteral("股票池的四维体检结果(修复率/回测/均线/PB 等)"));
        btnViewScan_->setToolTip(QStringLiteral("策略在全市场的形态选股结果(整点扫描; F7)"));
        auto* vg = new QButtonGroup(checkPage);
        vg->setExclusive(true);
        vg->addButton(btnViewCheck_);
        vg->addButton(btnViewScan_);
        vrow->addWidget(btnViewCheck_);
        vrow->addWidget(btnViewScan_);
        vrow->addStretch();
        cpl->addLayout(vrow);

        resultStack_ = new QStackedWidget();

        // 视图0: 个股体检 —— 自带筛选控件(两个视图各自带自己的控件, 不互相干扰)
        auto* p0 = new QWidget();
        auto* l0 = new QVBoxLayout(p0);
        l0->setContentsMargins(0, 0, 0, 0);
        if (filterBox_) l0->addWidget(filterBox_);   // 筛选条件(可折叠)
        if (presetBox_) l0->addWidget(presetBox_);   // 预设策略(一键配置)
        if (onlyMatch_) l0->addWidget(onlyMatch_);   // 仅显示满足条件的股票
        l0->addWidget(table_, 1);
        resultStack_->addWidget(p0);

        // 视图1: 形态筛选
        resultStack_->addWidget(scanTable_);

        cpl->addWidget(resultStack_, 1);
        rightTabs_->insertTab(0, checkPage, QStringLiteral("四维体检"));

        connect(btnViewCheck_, &QPushButton::clicked, this, [this]() {
            if (resultStack_) resultStack_->setCurrentIndex(0);
        });
        connect(btnViewScan_, &QPushButton::clicked, this, [this]() {
            if (resultStack_) resultStack_->setCurrentIndex(1);
        });
    }
    // ---- 新闻资讯 ----
    newsNM_ = new QNetworkAccessManager(this);
    newsTimer_ = new QTimer(this);
    connect(newsTimer_, &QTimer::timeout, this, [this]() {
        if (newsOffline()) { newsStatus_->setText(QStringLiteral("新闻源离线: 暂停自动刷新")); return; }
        updateNews();
    });

    auto* newsWidget = new QWidget();
    auto* newsLay = new QVBoxLayout(newsWidget);
    auto* newsCtl = new QHBoxLayout();
    btnUpdateNews_ = new QPushButton(QStringLiteral("\u27f3 更新新闻"));
    btnUpdateNews_->setMinimumHeight(28);
    btnUpdateNews_->setToolTip(QStringLiteral("抓取全部财经源并刷新列表"));
    connect(btnUpdateNews_, &QPushButton::clicked, this, &MainWindow::updateNews);
    newsCtl->addWidget(btnUpdateNews_);
    newsCtl->addWidget(new QLabel(QStringLiteral("分类: ")));
    newsFilter_ = new QComboBox();
    newsFilter_->addItem(QStringLiteral("全部"));
    newsFilter_->addItem(QStringLiteral("国内"));
    newsFilter_->addItem(QStringLiteral("国际"));
    newsFilter_->addItem(QStringLiteral("宏观"));
    connect(newsFilter_, &QComboBox::currentTextChanged, this, [this](const QString& t){ loadNewsView(t); });
    newsCtl->addWidget(newsFilter_);
    newsCtl->addWidget(new QLabel(QStringLiteral("自动刷新: ")));
    newsInterval_ = new QComboBox();
    newsInterval_->addItem(QStringLiteral("关闭"), 0);
    newsInterval_->addItem(QStringLiteral("15 分钟"), 15);
    newsInterval_->addItem(QStringLiteral("30 分钟"), 30);
    newsInterval_->addItem(QStringLiteral("60 分钟"), 60);
    newsInterval_->setCurrentIndex(2);
    connect(newsInterval_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        int mins = newsInterval_->currentData().toInt();
        if (mins > 0) { newsTimer_->start(mins * 60 * 1000); status_->setText(QStringLiteral("定时刷新: 每 %1 分钟自动更新").arg(mins)); }
        else { newsTimer_->stop(); status_->setText(QStringLiteral("定时刷新: 已关闭")); }
    });
    newsCtl->addWidget(new QLabel(QStringLiteral("双击打开: ")));
    newsOpenMode_ = new QComboBox();
    newsOpenMode_->addItem(QStringLiteral("内嵌浏览器"), 0);
    newsOpenMode_->addItem(QStringLiteral("导出到系统浏览器"), 1);
    newsOpenMode_->addItem(QStringLiteral("网上浏览"), 2);
    newsOpenMode_->setToolTip(QStringLiteral(
        "内嵌浏览器: 在程序窗口内用内嵌浏览器视图读本地缓存的正文(离线可读, Ctrl+滚轮缩放)\n"
        "导出到系统浏览器: 把正文导出成本地 HTML 文件, 再用系统浏览器打开(离线可读)\n"
        "网上浏览: 打开新闻原网页(需要联网)"));
    {
        QSettings st(QStringLiteral("stock_gui"), QStringLiteral("StockGui"));
        int m = st.value(QStringLiteral("news/openMode"), 0).toInt();
        newsOpenMode_->setCurrentIndex((m >= 0 && m <= 2) ? m : 0);
    }
    connect(newsOpenMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        QSettings st(QStringLiteral("stock_gui"), QStringLiteral("StockGui"));
        st.setValue(QStringLiteral("news/openMode"), idx);
    });
    newsCtl->addWidget(newsOpenMode_);
    newsCtl->addStretch();
    newsLay->addLayout(newsCtl);
    newsTable_ = new QTableWidget();
    QString nh[4] = { QStringLiteral("时间"), QStringLiteral("来源"), QStringLiteral("分类"), QStringLiteral("标题") };
    newsTable_->setColumnCount(4);
    newsTable_->setHorizontalHeaderLabels(QStringList() << nh[0] << nh[1] << nh[2] << nh[3]);
    newsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    newsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    newsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    newsTable_->setAlternatingRowColors(true);
    newsTable_->verticalHeader()->setVisible(false);
    newsTable_->horizontalHeader()->setStretchLastSection(true);
    newsTable_->setColumnWidth(0, 150);
    connect(newsTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { onNewsRowActivated(row); });
    newsLay->addWidget(newsTable_, 1);
    newsStatus_ = new QLabel(QStringLiteral("新闻: 尚未更新"));
    newsStatus_->setWordWrap(true);
    newsStatus_->setStyleSheet(QStringLiteral("color:#666;"));
    newsLay->addWidget(newsStatus_);
    rightTabs_->addTab(newsWidget, QStringLiteral("新闻资讯"));

    // ---- 实时盯盘: 每 N 秒拉一次实时行情, 用基线四维指标算买入评分与信号 ----
    {
        auto* watchPage = new QWidget();
        auto* wl = new QVBoxLayout(watchPage);
        wl->setContentsMargins(3, 3, 3, 3);

        auto* crow = new QHBoxLayout();
        crow->addWidget(new QLabel(QStringLiteral("每")));
        watchInterval_ = new QComboBox();
        watchInterval_->addItem(QStringLiteral("关闭"), 0);
        watchInterval_->addItem(QStringLiteral("30 秒"), 30);
        watchInterval_->addItem(QStringLiteral("1 分钟"), 60);
        watchInterval_->addItem(QStringLiteral("2 分钟"), 120);
        watchInterval_->addItem(QStringLiteral("5 分钟"), 300);
        watchInterval_->setCurrentIndex(2);          // 默认 1 分钟
        watchInterval_->setToolTip(QStringLiteral("自动检测间隔; 只拉实时快照, 不会重算回测"));
        crow->addWidget(watchInterval_);
        crow->addWidget(new QLabel(QStringLiteral("自动检测一次")));

        crow->addWidget(new QLabel(QStringLiteral("评分口径:")));
        watchMode_ = new QComboBox();
        watchMode_->addItem(QStringLiteral("短线打板/追涨"), 0);
        watchMode_->addItem(QStringLiteral("急杀反弹/低吸"), 1);
        watchMode_->setToolTip(QStringLiteral("打板: 位置越高、当日越强得分越高; 低吸: 位置越低、修复率越高得分越高"));
        connect(watchMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { watchTick(); });
        crow->addWidget(watchMode_);

        btnWatchNow_ = new QPushButton(QStringLiteral("立即检测"));
        connect(btnWatchNow_, &QPushButton::clicked, this, &MainWindow::watchTick);
        crow->addWidget(btnWatchNow_);

        auto* bImpCodes = new QPushButton(QStringLiteral("用【股票代码】框"));
        bImpCodes->setToolTip(QStringLiteral("把左侧股票代码框里的股票加入盯盘, 并重算一次基线指标"));
        connect(bImpCodes, &QPushButton::clicked, this, &MainWindow::watchImportFromCodes);
        crow->addWidget(bImpCodes);

        auto* bImpTable = new QPushButton(QStringLiteral("用当前体检结果"));
        bImpTable->setToolTip(QStringLiteral("把【四维体检】页表格里的股票加入盯盘"));
        connect(bImpTable, &QPushButton::clicked, this, &MainWindow::watchImportFromTable);
        crow->addWidget(bImpTable);

        auto* bImpSel = new QPushButton(QStringLiteral("用选中行"));
        bImpSel->setToolTip(QStringLiteral("只把【四维体检】表里当前选中的行加入盯盘(按住 Ctrl/Shift 可多选)"));
        connect(bImpSel, &QPushButton::clicked, this, &MainWindow::watchImportFromSelection);
        crow->addWidget(bImpSel);
        crow->addStretch();
        wl->addLayout(crow);

        watchTable_ = new QTableWidget();
        const QStringList wh = {
            QStringLiteral("代码"), QStringLiteral("名称"), QStringLiteral("现价"),
            QStringLiteral("涨跌%"), QStringLiteral("量比"), QStringLiteral("现价/MA20%"),
            QStringLiteral("60位"), QStringLiteral("修复率%"), QStringLiteral("回测%"),
            QStringLiteral("筹码获利%"), QStringLiteral("评分"), QStringLiteral("信号"),
            QStringLiteral("行情时间")
        };
        watchTable_->setColumnCount(wh.size());
        watchTable_->setHorizontalHeaderLabels(wh);
        watchTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        watchTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        watchTable_->setAlternatingRowColors(true);
        watchTable_->verticalHeader()->setVisible(false);
        watchTable_->horizontalHeader()->setStretchLastSection(true);
        watchTable_->setSortingEnabled(false);
        wl->addWidget(watchTable_, 1);

        // 自动检测定时器(默认 1 分钟)
        watchTimer_ = new QTimer(this);
        connect(watchTimer_, &QTimer::timeout, this, &MainWindow::watchTick);
        connect(watchInterval_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (!watchTimer_) return;
            const int sec = watchInterval_->currentData().toInt();
            if (sec > 0) {
                watchTimer_->start(sec * 1000);
                if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 每 %1 秒自动检测一次").arg(sec));
            } else {
                watchTimer_->stop();
                if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 自动检测已关闭(可点【立即检测】)"));
            }
        });
        watchTimer_->start(60 * 1000);   // 与默认选项(1 分钟)一致

        watchStatus_ = new QLabel(QStringLiteral("实时盯盘: 未开始 (先点【用股票代码框】导入)"));
        watchStatus_->setWordWrap(true);
        watchStatus_->setStyleSheet(QStringLiteral("color:#666;"));
        wl->addWidget(watchStatus_);

        rightTabs_->addTab(watchPage, QStringLiteral("实时盯盘"));
    }

    // (策略代码工作室已并入上面的「策略」页, 与回测结果上下分栏)
    rv->addWidget(rightTabs_, 1);

    status_ = new QLabel(QStringLiteral("初始化中..."));
    progress_ = new QProgressBar();
    progress_->setVisible(false);
    progress_->setMaximumWidth(240);
    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(status_, 1);
    statusRow->addWidget(progress_);
    rv->addLayout(statusRow);

    // ---- 分栏 ----
    splitter_ = new QSplitter(Qt::Horizontal);
    splitter_->addWidget(leftBox);
    splitter_->addWidget(right);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({ 350, 930 });
    mainLay->addWidget(splitter_);

    // ---- 恢复上次的界面状态(分割条位置 / 上次标签页 / 上次股票池) ----
    {
        QSettings st(QStringLiteral("stock_gui"), QStringLiteral("StockGui"));
        const QByteArray sp = st.value(QStringLiteral("ui/splitter")).toByteArray();
        if (!sp.isEmpty()) splitter_->restoreState(sp);
        const int tab = st.value(QStringLiteral("ui/lastTab"), 0).toInt();
        if (tab >= 0 && tab < rightTabs_->count()) rightTabs_->setCurrentIndex(tab);
        const QString codes = st.value(QStringLiteral("ui/codes")).toString();
        if (!codes.isEmpty() && codeEdit_) codeEdit_->setText(codes);
    }

    setCentralWidget(central);
}

void MainWindow::buildSidePanel(QVBoxLayout* sv) {
    // (更新数据 / 开始体检 / 全市场扫描 三个主操作已移到左侧顶部固定操作条)

    // ---------- 股票代码 ----------
    auto* gbCode = new QGroupBox(QStringLiteral("股票代码"));
    gbCode->setToolTip(QStringLiteral("多个代码用逗号或空格分隔"));
    auto* gl = new QVBoxLayout(gbCode);
    codeEdit_ = new QLineEdit();
    codeEdit_->setPlaceholderText(QStringLiteral("002385.SZ, 600519, 002730"));
    gl->addWidget(codeEdit_);
    auto* btnPool = new QPushButton(QStringLiteral("加载默认池 (19只)"));
    connect(btnPool, &QPushButton::clicked, this, &MainWindow::loadPool);
    gl->addWidget(btnPool);
    sv->addWidget(gbCode);

    // ---------- 策略参数 ----------
    auto* gbParam = new QGroupBox(QStringLiteral("体检口径"));
    gbParam->setToolTip(QStringLiteral("只影响【四维体检】页的修复率/回测列; 策略自身参数在 .lua 文件里"));
    auto* fl = new QFormLayout(gbParam);
    dipSpin_ = new QDoubleSpinBox();
    dipSpin_->setRange(-15, -3); dipSpin_->setDecimals(1); dipSpin_->setSingleStep(0.5);
    dipSpin_->setValue(-7); dipSpin_->setSuffix(QStringLiteral(" %"));
    fl->addRow(QStringLiteral("急杀阈值 ≤"), dipSpin_);
    tpSpin_ = new QDoubleSpinBox();
    tpSpin_->setRange(2, 20); tpSpin_->setDecimals(1); tpSpin_->setSingleStep(1);
    tpSpin_->setValue(5); tpSpin_->setSuffix(QStringLiteral(" %"));
    fl->addRow(QStringLiteral("反弹目标 ≥"), tpSpin_);
    holdSpin_ = new QSpinBox();
    holdSpin_->setRange(2, 10); holdSpin_->setValue(5); holdSpin_->setSuffix(QStringLiteral(" 日"));
    fl->addRow(QStringLiteral("持有窗口"), holdSpin_);
    {
        auto* hint = new QLabel(QStringLiteral("※ 只影响第1页体检表; 策略回测的参数在各 .lua 文件里"));
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:#888; font-size:11px;"));
        fl->addRow(hint);
    }
    settingsBox_ = gbParam;   // 低频项, 收进"设置"对话框(菜单 数据 → 设置…)

    // ---------- 筛选条件 ----------
    auto* gbF = new CollapsibleBox(QStringLiteral("筛选条件"), false /*默认收起*/);
    gbF->setToolTip(QStringLiteral("勾选后即时生效, 只影响结果视图, 不用重新扫描"));
    auto* grid = new QGridLayout();
    gbF->content()->addLayout(grid);
    int r = 0;

    fFixRateOn_ = new QCheckBox(QStringLiteral("修复率 ≥"));
    fFixRateVal_ = new QDoubleSpinBox();
    fFixRateVal_->setRange(0, 100); fFixRateVal_->setValue(60); fFixRateVal_->setSuffix(" %");
    grid->addWidget(fFixRateOn_, r, 0); grid->addWidget(fFixRateVal_, r, 1); r++;

    fSamplesOn_ = new QCheckBox(QStringLiteral("样本数 ≥"));
    fSamplesVal_ = new QSpinBox();
    fSamplesVal_->setRange(1, 50); fSamplesVal_->setValue(3);
    grid->addWidget(fSamplesOn_, r, 0); grid->addWidget(fSamplesVal_, r, 1); r++;

    fBacktestOn_ = new QCheckBox(QStringLiteral("回测 ≥"));
    fBacktestVal_ = new QDoubleSpinBox();
    fBacktestVal_->setRange(-50, 500); fBacktestVal_->setValue(0); fBacktestVal_->setSuffix(" %");
    grid->addWidget(fBacktestOn_, r, 0); grid->addWidget(fBacktestVal_, r, 1); r++;

    fWorstOn_ = new QCheckBox(QStringLiteral("最差单笔 ≥"));
    fWorstVal_ = new QDoubleSpinBox();
    fWorstVal_->setRange(-30, 0); fWorstVal_->setValue(-10); fWorstVal_->setSuffix(" %");
    grid->addWidget(fWorstOn_, r, 0); grid->addWidget(fWorstVal_, r, 1); r++;

    fZtOn_ = new QCheckBox(QStringLiteral("涨停数 ≥"));
    fZtVal_ = new QSpinBox();
    fZtVal_->setRange(1, 50); fZtVal_->setValue(2);
    grid->addWidget(fZtOn_, r, 0); grid->addWidget(fZtVal_, r, 1); r++;

    fPosOn_ = new QCheckBox(QStringLiteral("60日位置"));
    fPosLo_ = new QDoubleSpinBox(); fPosLo_->setRange(0, 100); fPosLo_->setValue(25); fPosLo_->setSuffix(" %");
    fPosHi_ = new QDoubleSpinBox(); fPosHi_->setRange(0, 100); fPosHi_->setValue(85); fPosHi_->setSuffix(" %");
    grid->addWidget(fPosOn_, r, 0);
    {
        auto* hb = new QHBoxLayout();
        hb->addWidget(fPosLo_);
        hb->addWidget(new QLabel(QStringLiteral("~")));
        hb->addWidget(fPosHi_);
        grid->addLayout(hb, r, 1);
    }
    r++;

    fMa20On_ = new QCheckBox(QStringLiteral("vsMA20 ≥"));
    fMa20Val_ = new QDoubleSpinBox();
    fMa20Val_->setRange(-20, 20); fMa20Val_->setValue(-5); fMa20Val_->setSuffix(" %");
    grid->addWidget(fMa20On_, r, 0); grid->addWidget(fMa20Val_, r, 1); r++;

    fPbOn_ = new QCheckBox(QStringLiteral("PB分位 ≤"));
    fPbVal_ = new QDoubleSpinBox();
    fPbVal_->setRange(0, 100); fPbVal_->setValue(80); fPbVal_->setSuffix(" %");
    grid->addWidget(fPbOn_, r, 0); grid->addWidget(fPbVal_, r, 1); r++;

    fChipProfitOn_ = new QCheckBox(QStringLiteral("筹码获利盘 ≤"));
    fChipProfitVal_ = new QDoubleSpinBox();
    fChipProfitVal_->setRange(0, 100); fChipProfitVal_->setValue(50); fChipProfitVal_->setSuffix(" %");
    fChipProfitVal_->setToolTip(QStringLiteral("获利盘少 = 多数筹码被套(潜在抛压小/底部区)"));
    grid->addWidget(fChipProfitOn_, r, 0); grid->addWidget(fChipProfitVal_, r, 1); r++;

    fChipConcOn_ = new QCheckBox(QStringLiteral("筹码集中70 ≥"));
    fChipConcVal_ = new QDoubleSpinBox();
    fChipConcVal_->setRange(0, 50); fChipConcVal_->setDecimals(1); fChipConcVal_->setValue(8);
    fChipConcVal_->setToolTip(QStringLiteral("70%筹码集中度 = 中间70%筹码的成本区间宽度, 越小越集中"));
    grid->addWidget(fChipConcOn_, r, 0); grid->addWidget(fChipConcVal_, r, 1); r++;

    fChipConc90On_ = new QCheckBox(QStringLiteral("筹码集中90 ≥"));
    fChipConc90Val_ = new QDoubleSpinBox();
    fChipConc90Val_->setRange(0, 80); fChipConc90Val_->setDecimals(1); fChipConc90Val_->setValue(15);
    fChipConc90Val_->setToolTip(QStringLiteral("90%筹码集中度 = 全部筹码成本区间宽度, 覆盖更全"));
    grid->addWidget(fChipConc90On_, r, 0); grid->addWidget(fChipConc90Val_, r, 1); r++;

    // 筛选条件不留在左侧栏, 而是挂到"四维体检"页顶部(与结果放在一起, 见 buildUI)
    filterBox_ = gbF;

    // ---------- 预设策略 ----------
    auto* gbPre = new QGroupBox(QStringLiteral("预设策略"));
    gbPre->setToolTip(QStringLiteral("一键套用常见筛选组合(会覆盖上面的筛选条件)"));
    auto* pg = new QGridLayout(gbPre);
    struct Preset { const char* name; int id; };
    Preset presets[] = {
        {"急杀反弹型", 0}, {"强势回踩型", 1},
        {"低位企稳型", 2}, {"筹码吸筹型", 3},
        {"全部显示", 4},
    };
    for (int i = 0; i < 5; i++) {
        auto* b = new QPushButton(QString::fromUtf8(presets[i].name));
        int id = presets[i].id;
        connect(b, &QPushButton::clicked, this, [this, id]() { applyPreset(id); });
        pg->addWidget(b, i / 2, i % 2);
    }
    presetBox_ = gbPre;       // 与筛选条件同属"筛结果", 挂到"四维体检"页顶部

    // ---------- 策略回测 (Lua 脚本) ----------
    auto* gbBt = new QGroupBox(QStringLiteral("策略回测 (Lua)"));
    gbBt->setToolTip(QStringLiteral("各策略的参数在 strategies/*.lua 文件顶部的 params 块里编辑"));
    auto* bvl = new QVBoxLayout(gbBt);
    stratCombo_ = new QComboBox();
    if (bt_) {
        for (auto& f : bt_->strategyFiles())
            stratCombo_->addItem(QString::fromStdString(bt_->displayName(f)),
                                 QString::fromStdString(f));
    }
    bvl->addWidget(stratCombo_);
    connect(stratCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onStrategyChanged);
    // 切换策略 -> 重载源码入代码工作室编辑区
    connect(stratCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::reloadSourceIntoEditor);

    // 策略语法检查 (编辑器地基: 运行/保存前拦截语法错误)
    {
        auto* row = new QHBoxLayout();
        btnCheckSyntax_ = new QPushButton(QStringLiteral("检查策略语法"));
        btnCheckSyntax_->setToolTip(QStringLiteral("只编译不运行; 发现语法错误即拦截, 不触发回测"));
        connect(btnCheckSyntax_, &QPushButton::clicked, this, &MainWindow::checkStrategySyntax);
        row->addWidget(btnCheckSyntax_);
        row->addStretch();
        stratCheckLabel_ = new QLabel("就绪");
        stratCheckLabel_->setWordWrap(true);
        stratCheckLabel_->setMinimumWidth(160);
        row->addWidget(stratCheckLabel_);
        bvl->addLayout(row);
    }

    // 策略参数 (动态渲染, 切换策略自动重建)
    auto* paramBox = new QGroupBox(QStringLiteral("策略参数 (选中的策略)"));
    stratParamLayout_ = new QFormLayout(paramBox);
    stratParamLayout_->setContentsMargins(6, 4, 6, 4);
    bvl->addWidget(paramBox);
    auto* btnReset = new QPushButton(QStringLiteral("恢复默认参数"));
    connect(btnReset, &QPushButton::clicked, this, &MainWindow::resetStrategyParams);
    bvl->addWidget(btnReset);

    // 参数工作室: 把调好的参数写回 .lua (仅改 value= 令牌, 保留注释/逻辑)
    auto* btnSaveParams = new QPushButton(QStringLiteral("保存参数到文件"));
    connect(btnSaveParams, &QPushButton::clicked, this, &MainWindow::saveStrategyParams);
    bvl->addWidget(btnSaveParams);
    btnBt1_ = new QPushButton(QStringLiteral("回测输入的代码"));
    connect(btnBt1_, &QPushButton::clicked, this, &MainWindow::runStrategyBacktest);
    bvl->addWidget(btnBt1_);
    btnBt2_ = new QPushButton(QStringLiteral("全市场回测排序"));
    connect(btnBt2_, &QPushButton::clicked, this, &MainWindow::runStrategyScan);
    bvl->addWidget(btnBt2_);
    btnBt3_ = new QPushButton(QStringLiteral("形态筛选 (全市场选股)"));
    {
        QFont bf2 = btnBt3_->font(); bf2.setBold(true);
        btnBt3_->setFont(bf2);
    }
    connect(btnBt3_, &QPushButton::clicked, this, &MainWindow::runFormScan);
    bvl->addWidget(btnBt3_);

    // ---------- 代码工作室 (内联编辑 on_bar/on_scan 源码) ----------
    auto* gbEd = new QGroupBox(QStringLiteral("策略代码"));
    gbEd->setToolTip(QStringLiteral("直接编辑 .lua 源码; 改完先点「检查策略语法」, 有语法错误不会写盘, 写前自动 .bak 备份"));
    auto* edvl = new QVBoxLayout(gbEd);
    // 专用代码编辑器: QScintilla + 官方 Lua 词法器
    // (行号栏 / 括号匹配 / 自动缩进 / 当前行高亮 / 撤销重做 / 折叠 都由它提供)
    srcEdit_ = new QsciScintilla();
    {
        QFont mono(QStringLiteral("Courier New"), 10);
        auto* lex = new QsciLexerLua(srcEdit_);   // 父对象=编辑器, 随编辑器析构
        lex->setDefaultFont(mono);
        srcEdit_->setLexer(lex);

        srcEdit_->setMarginsFont(mono);
        srcEdit_->setMarginType(0, QsciScintilla::NumberMargin);
        srcEdit_->setMarginLineNumbers(0, true);
        srcEdit_->setMarginWidth(0, QStringLiteral("00000"));
        srcEdit_->setBraceMatching(QsciScintilla::SloppyBraceMatch);
        srcEdit_->setAutoIndent(true);
        srcEdit_->setIndentationsUseTabs(false);
        srcEdit_->setTabWidth(4);
        srcEdit_->setTabIndents(true);
        srcEdit_->setBackspaceUnindents(true);
        srcEdit_->setCaretLineVisible(true);
        srcEdit_->setCaretLineBackgroundColor(QColor(0xf2, 0xf7, 0xff));
        srcEdit_->setWrapMode(QsciScintilla::WrapNone);
        srcEdit_->setUtf8(true);
        srcEdit_->setMinimumHeight(180);
    }
    edvl->addWidget(srcEdit_, 1);
    auto* edRow2 = new QHBoxLayout();
    btnSaveSource_ = new QPushButton(QStringLiteral("保存源码到文件"));
    btnSaveSource_->setToolTip(QStringLiteral("先编译校验; 通过且非空才写盘, 写前自动 .bak 备份"));
    btnReloadSource_ = new QPushButton(QStringLiteral("重载参数 (重读 params)"));
    btnReloadSource_->setToolTip(QStringLiteral("不改动文件, 重新读取 params 重建调参框"));
    btnResetSource_ = new QPushButton(QStringLiteral("重置源码"));
    btnResetSource_->setToolTip(QStringLiteral("把编辑区还原为磁盘上的原始内容"));
    edRow2->addWidget(btnSaveSource_);
    edRow2->addWidget(btnReloadSource_);
    edRow2->addWidget(btnResetSource_);
    edRow2->addStretch();
    edvl->addLayout(edRow2);
    connect(btnSaveSource_, &QPushButton::clicked, this, &MainWindow::saveStrategySource);
    connect(btnReloadSource_, &QPushButton::clicked, this, &MainWindow::reloadSourceIntoEditor);
    connect(btnResetSource_, &QPushButton::clicked, this, &MainWindow::resetStrategySource);
    auto* edHint = new QLabel(QStringLiteral("提示: 切换策略或保存成功后, 编辑区会刷新为最新内容."));
    edHint->setWordWrap(true);
    edHint->setStyleSheet(QStringLiteral("color:#888; font-size:11px;"));
    edvl->addWidget(edHint);
    strategyCtlBox_ = gbBt;   // 策略相关整体挪到右侧"策略"标签页

    // "策略代码工作室"不挂到左侧栏(那里太窄, 编辑区展不开), 而是作为
    // 右侧的独立标签页加入(见构造函数里 rightTabs_->addTab)。
    // 注意: buildSidePanel() 执行时 rightTabs_ 还没创建, 所以这里只暂存指针。
    codeStudioBox_ = gbEd;

    // 策略切换 -> 重载源码入编辑区 (已在上面 connect, 并在构造末尾初始化一次)

    // ---------- 显示选项 + 执行 ----------
    onlyMatch_ = new QCheckBox(QStringLiteral("仅显示满足条件的股票"));
    onlyMatch_->setChecked(false);
    // onlyMatch_(仅显示满足条件) 也归到"四维体检"页, 与结果放在一起

    fundFlowOn_ = new QCheckBox(QStringLiteral("详情查询主力资金流 (需联网)"));
    fundFlowOn_->setChecked(true);
    fundFlowOn_->setToolTip(QStringLiteral("关闭后完全离线工作; 离线环境下程序也会自动跳过"));
    // fundFlowOn_(详情查询主力资金流) 属低频/数据类选项, 收进"设置"对话框

    // (三个按钮已移到顶部操作条 / 菜单; "板块聚合统计" 见菜单 运行)

    // 筛选条件变化 -> 扫描视图即时刷新 (无需重新扫描)
    for (auto* b : {fFixRateOn_, fSamplesOn_, fBacktestOn_, fWorstOn_, fZtOn_, fPosOn_, fMa20On_, fPbOn_,
                    fChipProfitOn_, fChipConcOn_, fChipConc90On_})
        connect(b, &QCheckBox::toggled, this, &MainWindow::onFiltersChanged);
    for (auto* sp : {fFixRateVal_, fBacktestVal_, fWorstVal_, fPosLo_, fPosHi_, fMa20Val_, fPbVal_,
                     fChipProfitVal_, fChipConcVal_, fChipConc90Val_})
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onFiltersChanged);
    for (auto* sp : {fSamplesVal_, fZtVal_})
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onFiltersChanged);

    // 代码工作室: 构造时预填当前策略源码
    reloadSourceIntoEditor();
}

// ==================== 逻辑 ====================
AnalyzeParams MainWindow::currentParams() const {
    AnalyzeParams p;
    p.dipThreshold = dipSpin_->value();
    p.takeProfit = tpSpin_->value();
    p.holdDays = holdSpin_->value();
    return p;
}

bool MainWindow::matchFilters(const Stats& s) const {
    if (!s.ok) return false;
    if (fFixRateOn_->isChecked() && s.fixRate < fFixRateVal_->value()) return false;
    if (fSamplesOn_->isChecked() && s.events < fSamplesVal_->value()) return false;
    if (fBacktestOn_->isChecked() && s.backtest < fBacktestVal_->value()) return false;
    if (fWorstOn_->isChecked() && (s.events == 0 || s.worst < fWorstVal_->value())) return false;
    if (fZtOn_->isChecked() && s.ztCount < fZtVal_->value()) return false;
    if (fPosOn_->isChecked() && (s.pos60 < fPosLo_->value() || s.pos60 > fPosHi_->value())) return false;
    if (fMa20On_->isChecked() && s.vsMa20 < fMa20Val_->value()) return false;
    if (fPbOn_->isChecked() && (s.pbPct3y < 0 || s.pbPct3y > fPbVal_->value())) return false;
    if (fChipProfitOn_->isChecked() && (s.chipProfit < 0 || s.chipProfit > fChipProfitVal_->value())) return false;
    if (fChipConcOn_->isChecked() && (s.chipConc < 0 || s.chipConc < fChipConcVal_->value())) return false;
    if (fChipConc90On_->isChecked() && (s.chipConc90 < 0 || s.chipConc90 < fChipConc90Val_->value())) return false;
    return true;
}

void MainWindow::applyPreset(int id) {
    fFixRateOn_->setChecked(false); fSamplesOn_->setChecked(false);
    fBacktestOn_->setChecked(false); fWorstOn_->setChecked(false);
    fZtOn_->setChecked(false); fPosOn_->setChecked(false);
    fMa20On_->setChecked(false); fPbOn_->setChecked(false);
    fChipProfitOn_->setChecked(false); fChipConcOn_->setChecked(false);
    fChipConc90On_->setChecked(false);

    switch (id) {
    case 0:  // 急杀反弹型
        dipSpin_->setValue(-7); tpSpin_->setValue(5); holdSpin_->setValue(5);
        fFixRateOn_->setChecked(true);  fFixRateVal_->setValue(60);
        fSamplesOn_->setChecked(true);  fSamplesVal_->setValue(3);
        fBacktestOn_->setChecked(true); fBacktestVal_->setValue(0);
        fWorstOn_->setChecked(true);    fWorstVal_->setValue(-12);
        break;
    case 1:  // 强势回踩型
        fZtOn_->setChecked(true);       fZtVal_->setValue(2);
        fFixRateOn_->setChecked(true);  fFixRateVal_->setValue(50);
        fPosOn_->setChecked(true);      fPosLo_->setValue(20); fPosHi_->setValue(90);
        break;
    case 2:  // 低位企稳型
        fPosOn_->setChecked(true);      fPosLo_->setValue(0); fPosHi_->setValue(35);
        fMa20On_->setChecked(true);     fMa20Val_->setValue(-3);
        fPbOn_->setChecked(true);       fPbVal_->setValue(60);
        break;
    case 3:  // 筹码吸筹型
        fChipProfitOn_->setChecked(true); fChipProfitVal_->setValue(50);
        fChipConcOn_->setChecked(true);   fChipConcVal_->setValue(8);
        fPosOn_->setChecked(true); fPosLo_->setValue(0); fPosHi_->setValue(60);
        break;
    case 4:  // 全部显示
        onlyMatch_->setChecked(false);
        break;
    }
    status_->setText(QStringLiteral("已应用预设策略 #%1").arg(id + 1));
}

void MainWindow::runCheckup() {
    if (!engineReady_) { QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("数据库未连接")); return; }
    QString text = codeEdit_->text().trimmed();
    if (text.isEmpty()) { QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请输入股票代码")); return; }

    QStringList codes = text.split(QRegularExpression(QStringLiteral("[,\\s，]+")), Qt::SkipEmptyParts);
    AnalyzeParams params = currentParams();

    std::vector<Stats> all;
    int matched = 0;
    for (const QString& qc : codes) {
        std::string code = normalizeCode(qc);
        if (code.empty()) continue;
        std::string name = engine_.stockName(code);
        Stats s = engine_.analyze(code, name, 0, params);
        if (matchFilters(s)) matched++;
        all.push_back(s);
    }

    std::vector<Stats> show;
    for (auto& s : all)
        if (!onlyMatch_->isChecked() || matchFilters(s)) show.push_back(s);

    scanMode_ = false;
    fillTable(show);
    status_->setText(QStringLiteral("体检 %1 只 | 命中 %2 只 | 口径: 急杀≤%3% 止盈+%4% 窗口%5日")
        .arg(all.size()).arg(matched)
        .arg(params.dipThreshold, 0, 'f', 1)
        .arg(params.takeProfit, 0, 'f', 1)
        .arg(params.holdDays));
}

void MainWindow::loadPool() {
    codeEdit_->setText(QStringLiteral(
        "002730.SZ,603106.SH,003013.SZ,603867.SH,002515.SZ,000006.SZ,000572.SZ,002498.SZ,"
        "603005.SH,000158.SZ,603881.SH,600570.SH,300458.SZ,601138.SH,600330.SH,002333.SZ,"
        "601999.SH,002385.SZ,000713.SZ"));
}

void MainWindow::fillTable(const std::vector<Stats>& rows) {
    table_->setSortingEnabled(false);   // 填充期间关闭排序, 填充完再开(点击表头排序)
    table_->setUpdatesEnabled(false);
    table_->setRowCount((int)rows.size());
    for (int r = 0; r < (int)rows.size(); r++) {
        const Stats& s = rows[r];
        auto set = [&](int col, const QString& v) {
            table_->setItem(r, col, new QTableWidgetItem(v));
        };
        auto setN = [&](int col, const QString& v, double val) {
            table_->setItem(r, col, new NumericItem(v, val));
        };
        set(0, QString::fromStdString(s.code));
        set(1, QString::fromStdString(s.name));
        if (!s.ok) { set(20, QString::fromStdString(s.verdict)); continue; }
        setN(2, QString::number(s.price, 'f', 2), s.price);
        setN(3, QString::number(s.fixRate, 'f', 0) + "%", s.fixRate);
        setN(4, QString::number(s.events), s.events);
        setN(5, QString::asprintf("%+.1f%%", s.backtest), s.backtest);
        setN(6, s.events ? QString::asprintf("%+.1f%%", s.worst) : QStringLiteral("-"),
             s.events ? s.worst : -99999.0);
        setN(7, QString::number(s.ztCount), s.ztCount);
        setN(8, QString::number(s.pos60, 'f', 0) + "%", s.pos60);
        setN(9, QString::asprintf("%+.1f%%", s.vsMa20), s.vsMa20);
        // --- 扩展指标 ---
        setN(10, s.volRatio > 0 ? QString::number(s.volRatio, 'f', 2) : QStringLiteral("-"),
             s.volRatio > 0 ? s.volRatio : -1.0);
        setN(11, s.turnover > 0 ? QString::number(s.turnover, 'f', 1) : QStringLiteral("-"),
             s.turnover > 0 ? s.turnover : -1.0);
        setN(12, QString::asprintf("%+.1f%%", s.ret5), s.ret5);
        setN(13, QString::asprintf("%+.1f%%", s.ret20), s.ret20);
        setN(14, QString::number(s.amp, 'f', 1) + "%", s.amp);
        setN(15, QString::asprintf("%+.1f%%", s.fromHigh60), s.fromHigh60);
        setN(16, QString::number(s.amount5, 'f', 2), s.amount5);
        setN(17, s.mktcap > 0 ? QString::number(s.mktcap, 'f', 0) : QStringLiteral("-"),
             s.mktcap > 0 ? s.mktcap : -1.0);
        setN(18, s.chipProfit >= 0 ? QString::number(s.chipProfit, 'f', 0) + "%" : QStringLiteral("-"),
             s.chipProfit >= 0 ? s.chipProfit : -1.0);
        setN(19, s.chipConc >= 0 ? QString::number(s.chipConc, 'f', 1) : QStringLiteral("-"),
             s.chipConc >= 0 ? s.chipConc : -1.0);
        setN(20, s.chipConc90 >= 0 ? QString::number(s.chipConc90, 'f', 1) : QStringLiteral("-"),
             s.chipConc90 >= 0 ? s.chipConc90 : -1.0);
        setN(21, s.pbPct3y >= 0 ? QString::number(s.pbPct3y, 'f', 0) + "%" : QStringLiteral("-"),
             s.pbPct3y >= 0 ? s.pbPct3y : -1.0);
        bool hit = matchFilters(s);
        set(22, hit ? QStringLiteral("✓") : QString());
        if (hit) {
            if (auto* it = table_->item(r, 22)) it->setForeground(QColor(0x2e, 0x7d, 0x32));
        }
        set(23, QString::fromStdString(s.verdict));
    }
    table_->setUpdatesEnabled(true);
    table_->setSortingEnabled(true);
    if (rows.size() <= 800) table_->resizeColumnsToContents();
}

// ==================== 全市场扫描 ====================
void MainWindow::runScanAll() {
    if (!engineReady_) { QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("数据库未连接")); return; }
    auto stocks = engine_.allStocks();
    if (stocks.empty()) {
        status_->setText(QStringLiteral("扫描失败: 数据库无股票列表 (检查 kline_latest.db 是否在程序目录)"));
        return;
    }
    btnRun_->setEnabled(false);
    btnScan_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setRange(0, (int)stocks.size());
    progress_->setValue(0);

    AnalyzeParams params = currentParams();
    scanCache_.clear();
    scanCache_.reserve(stocks.size());
    int n = (int)stocks.size();
    int matched = 0;
    QElapsedTimer et; et.start();

    for (int i = 0; i < n; i++) {
        Stats st = engine_.analyze(stocks[i].first, stocks[i].second, 0, params);
        if (matchFilters(st)) matched++;
        scanCache_.push_back(std::move(st));
        if ((i & 127) == 127) {
            QApplication::processEvents();
            progress_->setValue(i + 1);
            status_->setText(QStringLiteral("全市场扫描中: %1/%2 ... 已命中 %3").arg(i + 1).arg(n).arg(matched));
        }
    }
    progress_->setValue(n);
    progress_->setVisible(false);
    btnRun_->setEnabled(true);
    btnScan_->setEnabled(true);
    scanMode_ = true;

    std::vector<Stats> show;
    for (auto& st : scanCache_) if (matchFilters(st)) show.push_back(st);
    fillTable(show);
    status_->setText(QStringLiteral("全市场扫描完成: %1 只 | 命中 %2 只 | 耗时 %3 秒 | 调整筛选条件可即时刷新")
        .arg(n).arg(matched).arg(et.elapsed() / 1000.0, 0, 'f', 1));
}

void MainWindow::refreshFromCache() {
    if (!scanMode_ || scanCache_.empty()) return;
    std::vector<Stats> show;
    for (auto& st : scanCache_) if (matchFilters(st)) show.push_back(st);
    fillTable(show);
    status_->setText(QStringLiteral("全市场扫描: %1 只 | 命中 %2 只 | 调整筛选条件即时刷新")
        .arg((int)scanCache_.size()).arg((int)show.size()));
}

void MainWindow::onFiltersChanged() {
    refreshFromCache();
}

// ==================== 策略能力切换 ====================
void MainWindow::onStrategyChanged() {
    if (!bt_ || stratCombo_->count() == 0) return;
    std::string f = stratCombo_->currentData().toString().toStdString();
    if (f.empty()) return;
    std::string path = resolveStrategiesDir() + "/" + f;
    bool canBt = luaStrategyHas(path, "on_bar");
    bool canScan = luaStrategyHas(path, "on_scan");
    if (btnBt1_) {
        btnBt1_->setEnabled(canBt);
        btnBt1_->setToolTip(canBt ? QString() : QStringLiteral("该策略未定义 on_bar, 不能回测"));
    }
    if (btnBt2_) {
        btnBt2_->setEnabled(canBt);
        btnBt2_->setToolTip(canBt ? QString() : QStringLiteral("该策略未定义 on_bar, 不能回测"));
    }
    if (btnBt3_) {
        btnBt3_->setEnabled(canScan);
        btnBt3_->setToolTip(canScan ? QString() : QStringLiteral("该策略未定义 on_scan, 不能筛选"));
    }
    rebuildParamWidgets();   // 重建参数控件
    checkStrategySyntax();   // P0: 切换策略自动复检语法
}

// ---------- 策略语法预检 (编辑器地基 P0) ==========
// 底层: 返回是否通过, 错误信息填 errMsg (供运行/保存前拦截)
bool MainWindow::strategySyntaxOk(const QString& luaFile, QString* errMsg) {
    QString f = luaFile.trimmed();
    if (f.isEmpty()) { if (errMsg) *errMsg = "未选择策略"; return false; }
    std::string err = luaSyntaxCheck(resolveStrategiesDir() + "/" + f.toStdString());
    if (!err.empty() && errMsg) *errMsg = QString::fromStdString(err);
    return err.empty();
}

void MainWindow::checkStrategySyntax() {
    if (!bt_ || !stratCombo_ || !stratCheckLabel_) return;
    QString f = stratCombo_->currentData().toString();
    if (f.isEmpty()) { stratCheckLabel_->setText("无策略"); return; }
    QString err;
    if (!strategySyntaxOk(f, &err)) {
        stratCheckLabel_->setText("✗ " + err);
        stratCheckLabel_->setStyleSheet("color:#c62828; font-weight:bold;");
        return;
    }
    std::string path = resolveStrategiesDir() + "/" + f.toStdString();
    bool canBt = luaStrategyHas(path, "on_bar");
    bool canScan = luaStrategyHas(path, "on_scan");
    QString cap = QString("语法正确 OK | %1 %2").arg(canBt ? "可回测" : "无 on_bar",
                                                    canScan ? "可选股" : "无 on_scan");
    stratCheckLabel_->setText(cap);
    stratCheckLabel_->setStyleSheet("color:#2e7d32; font-weight:bold;");
}

// ---------- 代码工作室: 重载源码入编辑区 (从磁盘重读当前策略) ==========
void MainWindow::reloadSourceIntoEditor() {
    if (!srcEdit_ || !stratCombo_ || stratCombo_->count() == 0) return;
    QString f = stratCombo_->currentData().toString();
    if (f.isEmpty()) { srcEdit_->setText(QStringLiteral("(未选择策略)")); return; }
    std::string path = resolveStrategiesDir() + "/" + f.toStdString();
    std::string content = readStrategyFile(path);
    if (content.empty()) {
        srcEdit_->setText(QStringLiteral("(无法读取该策略文件, 请先保存其它工作)"));
        return;
    }
    // 依文件实际的换行符设置 EOL, 否则保存时可能被整体改成 CRLF
    srcEdit_->setEolMode(content.find("\r\n") != std::string::npos
                         ? QsciScintilla::EolWindows : QsciScintilla::EolUnix);
    srcEdit_->setText(QString::fromStdString(content));
}

// ---------- 代码工作室: 把编辑区内容写回 .lua (先编译校验, 写前 .bak) ========
void MainWindow::saveStrategySource() {
    if (!srcEdit_ || !stratCombo_ || stratCombo_->count() == 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("未选择策略")); return; }
    QString f = stratCombo_->currentData().toString();
    if (f.isEmpty()) { QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("未选择策略")); return; }
    std::string path = resolveStrategiesDir() + "/" + f.toStdString();
    std::string content = srcEdit_->text().toStdString();
    if (content.empty()) {
        QMessageBox::information(this, QStringLiteral("参数工作室"), QStringLiteral("编辑区为空, 不写入。")); return;
    }
    std::string err;
    if (!luaWriteStrategyFile(path, content, &err)) {
        QMessageBox::critical(this, QStringLiteral("参数工作室"),
            QString("保存失败:\n\n") + err.c_str());
        return;
    }
    // 写成功: 重建参数 + 复检语法 + 刷新状态栏
    rebuildParamWidgets();
    checkStrategySyntax();
    status_->setText(QStringLiteral("已保存 %1, 共 %2 字节\n(原文件已自动备份为 %1.bak)").arg(f)
        .arg((int)content.size()));
}

// ---------- 代码工作室: 编辑区还原磁盘原文 ========
void MainWindow::resetStrategySource() {
    if (!srcEdit_ || !stratCombo_ || stratCombo_->count() == 0) return;
    QString f = stratCombo_->currentData().toString();
    if (f.isEmpty()) return;
    std::string path = resolveStrategiesDir() + "/" + f.toStdString();
    std::string content = readStrategyFile(path);
    srcEdit_->setText(content.empty()
        ? QStringLiteral("(无法读取该策略文件)")
        : QString::fromStdString(content));
}

// ==================== 个股详情弹窗 ====================
void MainWindow::showStockDetail(int row, int col) {
    (void)col;
    if (row < 0 || row >= table_->rowCount()) return;
    QTableWidgetItem* it0 = table_->item(row, 0);
    if (!it0) return;
    showStockDetailByCode(it0->text().toStdString());
}

void MainWindow::showStockDetailByCode(const std::string& code) {
    std::string name = engine_.stockName(code);
    Stats st = engine_.analyze(code, name, 0, currentParams());
    if (!st.ok && st.verdict == "数据不足") {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("该股票数据不足, 无法生成详情"));
        return;
    }
    auto bars = engine_.loadSeries(code, 600);
    int n = (int)bars.size();
    double pe = engine_.latestPe(code);
    std::string ind = engine_.industryOf(code);
    std::string cons = engine_.conceptsOf(code);

    // 近20/60日高低
    double hi20 = 0, lo20 = 1e18, hi60 = 0, lo60 = 1e18;
    for (int i = std::max(0, n - 60); i < n; i++) {
        hi60 = std::max(hi60, bars[i].high); lo60 = std::min(lo60, bars[i].low);
        if (i >= n - 20) { hi20 = std::max(hi20, bars[i].high); lo20 = std::min(lo20, bars[i].low); }
    }
    double px = n > 0 ? bars[n-1].close : 0;
    double headroom = hi60 > 0 ? (hi60 - px) / hi60 * 100 : 0;

    // 筹码分布 + PE分位 + 区间涨幅 + 月度
    auto chips = engine_.chipDist(bars, px);
    double pePct = engine_.pePercentile3y(code, pe);
    double r60 = (n >= 61) ? (bars[n-1].close - bars[n-61].close) / bars[n-61].close * 100 : 0;
    QString monthPerf;
    {
        std::map<std::string, std::pair<double,double>> mon;
        for (auto& b : bars) {
            std::string ym = b.date.substr(0, 7);
            auto it = mon.find(ym);
            if (it == mon.end()) mon[ym] = {b.close, b.close};
            else it->second.second = b.close;
        }
        std::vector<std::pair<std::string, std::pair<double,double>>> v(mon.begin(), mon.end());
        int startM = std::max(0, (int)v.size() - 6);
        for (int i = startM; i < (int)v.size(); i++) {
            double chg = v[i].second.first > 0
                       ? (v[i].second.second - v[i].second.first) / v[i].second.first * 100 : 0;
            if (!monthPerf.isEmpty()) monthPerf += "   |   ";
            monthPerf += QString("%1: %2%").arg(QString::fromStdString(v[i].first)).arg(chg, 0, 'f', 1);
        }
    }

    QString mk = "纠缠";
    if (st.ma5 > st.ma20 && st.ma20 > st.ma60) mk = "多头排列";
    else if (st.ma5 < st.ma20 && st.ma20 < st.ma60) mk = "空头排列";

    QString html;
    html += QString("<h2 style='margin:4px'>%1 %2</h2>")
        .arg(QString::fromStdString(code), QString::fromStdString(name));
    html += QString("<p style='margin:4px;color:#555'>现价 <b>%1</b> | 60日位 %2%% | vsMA20 %3% | %4</p>")
        .arg(px, 0, 'f', 2).arg(st.pos60, 0, 'f', 0)
        .arg(st.vsMa20, 0, 'f', 1).arg(QString::fromStdString(st.verdict));
    if (!ind.empty() || !cons.empty())
        html += QString("<p style='margin:4px;color:#777'>行业: %1<br>概念: %2</p>")
            .arg(QString::fromStdString(ind), QString::fromStdString(cons));

    html += "<h3 style='margin:6px 4px'>均线</h3><table border='1' cellspacing='0' cellpadding='3' style='margin:2px'>";
    html += QString("<tr><td>MA5</td><td>%1</td><td>MA10</td><td>%2</td><td>MA20</td><td>%3</td><td>MA60</td><td>%4</td><td><b>%5</b></td></tr>")
        .arg(st.ma5, 0, 'f', 2).arg(st.ma10, 0, 'f', 2).arg(st.ma20, 0, 'f', 2)
        .arg(st.ma60, 0, 'f', 2).arg(mk);
    html += "</table>";

    html += "<h3 style='margin:6px 4px'>压力 / 支撑</h3><table border='1' cellspacing='0' cellpadding='3' style='margin:2px'>";
    html += QString("<tr><td>20日高</td><td>%1</td><td>20日低</td><td>%2</td></tr>")
        .arg(hi20, 0, 'f', 2).arg(lo20, 0, 'f', 2);
    html += QString("<tr><td>60日高</td><td>%1</td><td>60日低</td><td>%2</td></tr>")
        .arg(hi60, 0, 'f', 2).arg(lo60, 0, 'f', 2);
    html += QString("<tr><td>距60日高空间</td><td>%1%</td><td>距60日低</td><td>+%2%</td></tr>")
        .arg(headroom, 0, 'f', 1).arg(lo60 > 0 ? (px - lo60) / lo60 * 100 : 0, 0, 'f', 1);
    html += "</table>";

    html += "<h3 style='margin:6px 4px'>估值</h3><table border='1' cellspacing='0' cellpadding='3' style='margin:2px'>";
    html += QString("<tr><td>PB(最新)</td><td>%1</td><td>PB分位(3年)</td><td>%2</td>"
                    "<td>PE(TTM)</td><td>%3</td><td>PE分位</td><td>%4</td></tr>")
        .arg(st.pbCurrent > 0 ? QString::number(st.pbCurrent, 'f', 2) : QStringLiteral("-"),
             st.pbPct3y >= 0 ? QString::number(st.pbPct3y, 'f', 0) + "%" : QStringLiteral("-"),
             pe > 0 ? QString::number(pe, 'f', 1) : QStringLiteral("-"),
             pePct >= 0 ? QString::number(pePct, 'f', 0) + "%" : QStringLiteral("-"));
    html += "</table>";

    html += "<h3 style='margin:6px 4px'>行为统计 (急杀反弹口径)</h3><table border='1' cellspacing='0' cellpadding='3' style='margin:2px'>";
    html += QString("<tr><td>修复率</td><td>%1%</td><td>样本</td><td>%2</td><td>回测</td><td>%3%</td></tr>")
        .arg(st.fixRate, 0, 'f', 0).arg(st.events).arg(st.backtest, 0, 'f', 1);
    html += QString("<tr><td>最差单笔</td><td>%1%</td><td>涨停次数</td><td>%2</td><td></td><td></td></tr>")
        .arg(st.events ? st.worst : 0, 0, 'f', 1).arg(st.ztCount);
    html += "</table>";

    // 动能与涨幅
    html += "<h3 style='margin:6px 4px'>动能与区间涨幅</h3><table border='1' cellspacing='0' cellpadding='3' style='margin:2px'>";
    html += QString("<tr><td>动能</td><td><b>%1</b></td><td>5日</td><td>%2%</td><td>20日</td><td>%3%</td><td>60日</td><td>%4%</td></tr>")
        .arg(st.emaRedDays > 0 ? QStringLiteral("红柱%1天").arg(st.emaRedDays) : QStringLiteral("黄(未启动)"),
             QString::number(st.ret5, 'f', 1), QString::number(st.ret20, 'f', 1),
             QString::number(r60, 'f', 1));
    html += QString("<tr><td>量比</td><td>%1</td><td>换手率</td><td>%2</td><td>成交额(5日均)</td><td>%3亿</td><td>市值</td><td>%4亿</td></tr>")
        .arg(st.volRatio > 0 ? QString::number(st.volRatio, 'f', 2) : QStringLiteral("-"),
             st.turnover > 0 ? QString::number(st.turnover, 'f', 1) + "%" : QStringLiteral("-"),
             QString::number(st.amount5, 'f', 2),
             st.mktcap > 0 ? QString::number(st.mktcap, 'f', 0) : QStringLiteral("-"));
    html += QString("<tr><td colspan='8' style='color:#666'>月度: %1</td></tr>").arg(monthPerf);
    html += "</table>";

    // 筹码分布
    if (chips.ok) {
        html += "<h3 style='margin:6px 4px'>筹码分布 (近260日近似)</h3><table border='1' cellspacing='0' cellpadding='3' style='margin:2px'>";
        html += QString("<tr><td>获利盘</td><td><b>%1%</b></td><td>平均成本</td><td>%2</td><td>现价 vs 成本</td><td>%3%</td><td>集中度70</td><td>%4%</td></tr>")
            .arg(chips.profitRatio, 0, 'f', 0)
            .arg(chips.cost50, 0, 'f', 2)
            .arg(chips.cost50 > 0 ? (px - chips.cost50) / chips.cost50 * 100 : 0, 0, 'f', 1)
            .arg(chips.conc70, 0, 'f', 1);
        html += QString("<tr><td>70%%成本区间</td><td>%1 ~ %2</td><td colspan='2'>（多数筹码成本带）</td>"
                        "<td>集中度90</td><td>%3%</td><td></td><td></td></tr>")
            .arg(chips.cost15, 0, 'f', 2).arg(chips.cost85, 0, 'f', 2).arg(chips.conc90, 0, 'f', 1);
        html += QString("<tr><td>90%%成本区间</td><td>%1 ~ %2</td><td colspan='6'></td></tr>")
            .arg(chips.cost5, 0, 'f', 2).arg(chips.cost95, 0, 'f', 2);
        html += "</table>";
    }

    // 主力资金流 (网络, 容错; 可关闭/离线自动跳过)
    if (!fundFlowOn_->isChecked()) {
        html += "<h3 style='margin:6px 4px'>主力资金流</h3>"
                "<p style='margin:4px;color:#999'>已关闭 (左侧面板可开启)</p>";
    } else {
        FundFlow ff = fetchFundFlow(QString::fromStdString(code));
        html += "<h3 style='margin:6px 4px'>主力资金流</h3>";
        if (ff.ok) {
            bool mainLead = ff.main5 > 0 && ff.main5 > ff.retail5;
            html += QString("<table border='1' cellspacing='0' cellpadding='3' style='margin:2px'>"
                            "<tr><td>窗口</td><td>近%1日</td><td>主力净流入</td><td><b>%2</b> 万</td>"
                            "<td>散户+中单</td><td>%3 万</td><td>判定</td>"
                            "<td style='color:%4'><b>%5</b></td></tr></table>")
                .arg(ff.days)
                .arg(ff.main5, 0, 'f', 0)
                .arg(ff.retail5, 0, 'f', 0)
                .arg(mainLead ? QStringLiteral("#c62828") : QStringLiteral("#2e7d32"))
                .arg(mainLead ? QStringLiteral("主力主导 ✓") : QStringLiteral("散户主导 ✗"));
        } else {
            html += QString("<p style='margin:4px;color:#999'>获取失败 (%1)</p>").arg(ff.err);
        }
    }

    // 近10日K线
    html += "<h3 style='margin:6px 4px'>近10日行情</h3><table border='1' cellspacing='0' cellpadding='3' style='margin:2px'>";
    html += "<tr style='background:#eee'><td>日期</td><td>开</td><td>高</td><td>低</td><td>收</td><td>涨跌%</td><td>量(万手)</td></tr>";
    for (int i = std::max(0, n - 10); i < n; i++) {
        double prev = (i > 0) ? bars[i-1].close : bars[i].open;
        double pct = prev > 0 ? (bars[i].close - prev) / prev * 100 : 0;
        QString color = pct >= 0 ? "#c62828" : "#2e7d32";
        html += QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td><b>%5</b></td>"
                        "<td style='color:%6'>%7</td><td>%8</td></tr>")
            .arg(QString::fromStdString(bars[i].date))
            .arg(bars[i].open, 0, 'f', 2).arg(bars[i].high, 0, 'f', 2)
            .arg(bars[i].low, 0, 'f', 2).arg(bars[i].close, 0, 'f', 2)
            .arg(color).arg(pct, 0, 'f', 2).arg(bars[i].volume / 1e4, 0, 'f', 1);
    }
    html += "</table>";

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("个股详情 — %1 %2").arg(QString::fromStdString(code),
                                                              QString::fromStdString(name)));
    auto* lay = new QVBoxLayout(&dlg);
    auto* tabs = new QTabWidget(&dlg);
    // Tab1: K线图 (蜡烛+均线+成交量, 悬停看单日, 滚轮缩放)
    auto* chart = new KLineChart();
    chart->setData(bars, QString::fromStdString(code + "  " + name));
    tabs->addTab(chart, QStringLiteral("K线图"));
    // Tab2: 数据概况 (文本报告)
    auto* tb = new QTextBrowser();
    tb->setHtml(html);
    tabs->addTab(tb, QStringLiteral("数据概况"));
    // Related news: search news.db cache; double-click opens the source page
    auto* newsPage = new QWidget();
    auto* nlay = new QVBoxLayout(newsPage);
    auto* newsTable = new QTableWidget();
    QStringList ncol = { QString::fromUtf8("Time"), QString::fromUtf8("Source"), QString::fromUtf8("Category"), QString::fromUtf8("Title") };
    newsTable->setColumnCount(ncol.size());
    newsTable->setHorizontalHeaderLabels(ncol);
    newsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    newsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    newsTable->setAlternatingRowColors(true);
    newsTable->verticalHeader()->setVisible(false);
    nlay->addWidget(newsTable, 1);
    tabs->addTab(newsPage, QString::fromUtf8("News"));
    {
        QString dbPath = QString::fromStdString(resolveDbPath("news.db"));
        auto rows = searchNews(dbPath.toStdString(), code, 20);
        newsTable->setRowCount(0);
        for (auto& it : rows) {
            int r = newsTable->rowCount();
            newsTable->insertRow(r);
            QString ts = it.epoch > 0
                ? QDateTime::fromSecsSinceEpoch(it.epoch).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                : QString();
            auto* ti = new QTableWidgetItem(QString::fromStdString(it.title));
            ti->setData(Qt::UserRole, QString::fromStdString(it.link));
            newsTable->setItem(r, 0, new QTableWidgetItem(ts));
            newsTable->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(it.source)));
            newsTable->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(it.category)));
            newsTable->setItem(r, 3, ti);
        }
        newsTable->horizontalHeader()->setStretchLastSection(true);
        connect(newsTable, &QTableWidget::cellDoubleClicked, this, [this, newsTable](int row, int) {
            if (row < 0) return;
            QTableWidgetItem* t = newsTable->item(row, 3);
            if (!t) return;
            NewsItem it;
            it.link = t->data(Qt::UserRole).toString().toStdString();
            it.title = t->text().toStdString();
            if (newsTable->item(row, 1)) it.source = newsTable->item(row, 1)->text().toStdString();
            if (newsTable->item(row, 2)) it.category = newsTable->item(row, 2)->text().toStdString();
            showNewsReader(it);   // 个股相关新闻同样走本地阅读器(离线可读)
        });
        if (rows.empty())
            status_->setText(QStringLiteral("%1: no related news cached, click [Update] to fetch.")
                .arg(QString::fromStdString(name)));
        else
            status_->setText(QStringLiteral("%1 related news: %2 items.")
                .arg(QString::fromStdString(name)).arg((int)rows.size()));
    }
    lay->addWidget(tabs);
    dlg.resize(900, 700);
    dlg.exec();
}

// ==================== 板块聚合统计 (树形: 板块 -> 具体股票) ====================
void MainWindow::showSectorStats() {
    int rows = table_->rowCount();
    if (rows == 0) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("当前表格为空。先做一次体检/扫描, 再统计。"));
        return;
    }
    // 收集: 板块 -> [(完整代码, "002385  大北农"), ...]
    QHash<QString, QVector<QPair<QString, QString>>> indMap, conMap;
    int nInd = 0, nCon = 0;
    for (int r = 0; r < rows; r++) {
        auto* it0 = table_->item(r, 0);
        if (!it0) continue;
        std::string code = it0->text().toStdString();
        auto* it1 = table_->item(r, 1);
        QString name = it1 ? it1->text() : QString();
        QString c6 = QString::fromStdString(code).section('.', 0, 0);
        QString label = c6 + "  " + name;
        std::string ind = engine_.industryOf(code);
        if (!ind.empty()) {
            indMap[QString::fromStdString(ind)].append(qMakePair(QString::fromStdString(code), label));
            nInd++;
        }
        std::string cons = engine_.conceptsOf(code);
        if (!cons.empty()) {
            for (const QString& c : QString::fromStdString(cons).split(';', Qt::SkipEmptyParts)) {
                QString cc = c.trimmed();
                if (!cc.isEmpty()) {
                    conMap[cc].append(qMakePair(QString::fromStdString(code), label));
                    nCon++;
                }
            }
        }
    }

    auto makeTree = [&](const QString& title, QHash<QString, QVector<QPair<QString, QString>>>& m) {
        auto* box = new QGroupBox(title);
        auto* v = new QVBoxLayout(box);
        auto* tree = new QTreeWidget();
        tree->setColumnCount(2);
        tree->setHeaderLabels({QStringLiteral("板块 / 股票 (双击看详情)"), QStringLiteral("数量")});
        tree->setColumnWidth(0, 330);
        tree->setAlternatingRowColors(true);
        // 顶级节点: 按数量降序
        QList<QPair<int, QString>> keys;
        for (auto it = m.constBegin(); it != m.constEnd(); ++it)
            keys.append(qMakePair(it.value().size(), it.key()));
        std::sort(keys.begin(), keys.end(),
                  [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                      return a.first > b.first;
                  });
        for (const auto& kv : keys) {
            auto* top = new QTreeWidgetItem(tree);
            top->setText(0, kv.second);
            top->setText(1, QString::number(kv.first));
            top->setForeground(0, QColor(0x15, 0x65, 0xc0));
            // 子节点: 该板块下的具体股票 (按代码升序)
            auto list = m.value(kv.second);
            std::sort(list.begin(), list.end(),
                      [](const QPair<QString, QString>& a, const QPair<QString, QString>& b) {
                          return a.first < b.first;
                      });
            for (const auto& pr : list) {
                auto* child = new QTreeWidgetItem(top);
                child->setText(0, pr.second);
                child->setData(0, Qt::UserRole, pr.first);   // 完整代码, 供双击详情
            }
        }
        // 双击股票 -> 详情弹窗
        connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
            QString c = item->data(0, Qt::UserRole).toString();
            if (!c.isEmpty()) showStockDetailByCode(c.toStdString());
        });
        // 展开/折叠 + 提示
        auto* btnRow = new QHBoxLayout();
        auto* bExp = new QPushButton(QStringLiteral("展开全部"));
        auto* bCol = new QPushButton(QStringLiteral("折叠全部"));
        connect(bExp, &QPushButton::clicked, tree, &QTreeWidget::expandAll);
        connect(bCol, &QPushButton::clicked, tree, &QTreeWidget::collapseAll);
        btnRow->addWidget(bExp);
        btnRow->addWidget(bCol);
        btnRow->addStretch();
        auto* hint = new QLabel(QStringLiteral("双击股票行 → 弹出详情"));
        hint->setStyleSheet(QStringLiteral("color:#888;"));
        btnRow->addWidget(hint);
        v->addWidget(tree);
        v->addLayout(btnRow);
        return box;
    };

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("板块聚合统计 — %1 只 | 行业覆盖 %2 | 概念覆盖 %3")
        .arg(rows).arg(nInd).arg(nCon));
    auto* lay = new QHBoxLayout(&dlg);
    lay->addWidget(makeTree(QStringLiteral("行业分布"), indMap));
    lay->addWidget(makeTree(QStringLiteral("概念分布"), conMap));
    dlg.resize(920, 640);
    dlg.exec();
}

// ==================== 数据更新（两级模式）====================
// 快速更新: 批量快照(14个请求)补全市场最新一日 —— 秒级, 温柔请求量
// 缺口询问: 若中间有多日缺口 -> 询问是否全量重建(慢速安全模式)
void MainWindow::updateData() {
    if (!engineReady_) { QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("数据库未连接")); return; }
    QMessageBox mb(QMessageBox::Question, QStringLiteral("更新数据"),
        QStringLiteral("快速更新: 拉取全市场最新行情快照(约10-20秒, 请求量极低)。\n"
                       "若检测到历史缺口会再询问是否全量重建。\n"
                       "提示: 收盘后(15:00后)更新得到当日最终数据。\n\n继续?"),
        QMessageBox::Yes | QMessageBox::No, this);
    QCheckBox* cbVal = new QCheckBox(QStringLiteral("同时更新估值数据 (PE/PB分位最新化, 约1-3分钟)"), &mb);
    cbVal->setChecked(true);
    mb.setCheckBox(cbVal);
    if (mb.exec() != QMessageBox::Yes) return;
    bool alsoVal = cbVal->isChecked();

    QString beforeMax = QString::fromStdString(engine_.latestMarketDate());
    QString dbp = QString::fromStdString(resolveDbPath("kline_latest.db"));

    btnUpdate_->setEnabled(false);
    btnRun_->setEnabled(false); btnScan_->setEnabled(false);
    btnBt1_->setEnabled(false); btnBt2_->setEnabled(false); btnBt3_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    status_->setText(QStringLiteral("正在更新(快速模式)..."));

    engine_.close();          // 释放数据库句柄(Windows 文件锁)
    engineReady_ = false;

    QString err;
    int r = updateIncremental(dbp,
        [this](int d, int t) {
            progress_->setRange(0, t);
            progress_->setValue(d);
            status_->setText(QStringLiteral("拉取行情快照: %1/%2 ...").arg(d).arg(t));
        }, &err, false);

    std::string e2;
    engineReady_ = engine_.open(e2);
    if (r == 1 || r == 2) resetFundFlowOffline();   // 网络可用, 恢复资金流查询

    auto restoreButtons = [this]() {
        progress_->setVisible(false);
        btnUpdate_->setEnabled(true);
        btnRun_->setEnabled(true); btnScan_->setEnabled(true);
        onStrategyChanged();
    };

    // 估值更新(可选, 独立容错; 行情失败时仍执行)
    auto runValUpdate = [this, alsoVal]() {
        if (!alsoVal) return;
        progress_->setRange(0, 100); progress_->setValue(0);
        status_->setText(QStringLiteral("更新估值数据(PE/PB)..."));
        engine_.close(); engineReady_ = false;
        QString verr;
        bool vok = updateValuationDb(QString::fromStdString(resolveDbPath("valuation.db")), 0,
            [this](int d, int t) {
                progress_->setRange(0, t); progress_->setValue(d);
                status_->setText(QStringLiteral("更新估值数据: %1/%2 ...").arg(d).arg(t));
            }, &verr);
        std::string e4;
        engineReady_ = engine_.open(e4);
        status_->setText(status_->text() +
            (vok ? QStringLiteral(" | 估值数据已更新 ✓")
                 : QStringLiteral(" | 估值更新失败: %1").arg(verr)));
    };

    if (r == 0) {
        runValUpdate();
        restoreButtons();
        status_->setText(QStringLiteral("更新失败: %1 (原数据未受影响)").arg(err));
        QMessageBox::warning(this, QStringLiteral("更新失败"),
            QStringLiteral("快速更新失败:\n%1\n\n原数据库未被修改, 可继续使用。\n"
                           "(行情接口可能临时限流, 过 15-30 分钟重试即可)").arg(err));
        return;
    }
    if (r == 2) {
        runValUpdate();
        restoreButtons();
        status_->setText(QStringLiteral("数据已是最新 (最后交易日: %1)").arg(beforeMax));
        QMessageBox::information(this, QStringLiteral("已是最新"),
            QStringLiteral("数据库已包含最新交易日(%1)的数据, 无需更新。").arg(beforeMax));
        return;
    }

    // r == 1: 快速更新成功 -> 缺口检查
    QString afterMax = QString::fromStdString(engine_.latestMarketDate());
    QDate d1 = QDate::fromString(beforeMax, QStringLiteral("yyyy-MM-dd"));
    QDate d2 = QDate::fromString(afterMax, QStringLiteral("yyyy-MM-dd"));
    int gapDays = (d1.isValid() && d2.isValid()) ? (int)d1.daysTo(d2) : 0;

    if (gapDays > 3) {
        auto ret = QMessageBox::question(this, QStringLiteral("检测到历史缺口"),
            QStringLiteral("最新数据已补到 %1。\n\n"
                           "但从 %2 到 %3 之间有约 %4 个自然日的缺口\n"
                           "(快照只能补最新一天, 中间缺口需要逐只拉取)。\n\n"
                           "是否全量重建补齐缺口?\n"
                           "慢速安全模式: 逐只拉取260根历史, 约10-20分钟。")
                .arg(afterMax, beforeMax, afterMax).arg(gapDays),
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            progress_->setRange(0, 100); progress_->setValue(0);
            status_->setText(QStringLiteral("全量重建中(慢速安全模式)..."));
            engine_.close(); engineReady_ = false;
            QString err2;
            bool ok2 = updateMarketData(dbp, 0,
                [this](int d, int t) {
                    progress_->setRange(0, t);
                    progress_->setValue(d);
                    status_->setText(QStringLiteral("全量重建中: %1/%2 (慢速安全模式, 可稍等)").arg(d).arg(t));
                }, &err2, false);
            std::string e3;
            engineReady_ = engine_.open(e3);
            if (!ok2) {
                status_->setText(QStringLiteral("全量重建失败: %1 (原数据未受影响)").arg(err2));
                QMessageBox::warning(this, QStringLiteral("全量重建失败"), err2);
            } else {
                status_->setText(QStringLiteral("全量重建完成 ✓ 最新交易日: %1 | 请重新体检/扫描")
                    .arg(QString::fromStdString(engine_.latestMarketDate())));
            }
        } else {
            status_->setText(QStringLiteral("更新完成(快速模式) ✓ 最新: %1 | 中间缺口未补齐").arg(afterMax));
        }
    } else {
        status_->setText(QStringLiteral("更新完成 ✓ 最新交易日: %1 | 请重新体检/扫描").arg(afterMax));
    }
    runValUpdate();
    restoreButtons();
}

// ==================== 策略参数控件 ====================
void MainWindow::rebuildParamWidgets() {
    if (!stratParamLayout_) return;
    while (stratParamLayout_->rowCount() > 0) {
        stratParamLayout_->removeRow(0);
    }
    spWidgets_.clear();
    if (stratCombo_->count() == 0) return;
    std::string f = stratCombo_->currentData().toString().toStdString();
    if (f.empty()) return;
    auto defs = luaReadParams(resolveStrategiesDir() + "/" + f);
    if (defs.empty()) {
        stratParamLayout_->addRow(new QLabel(QStringLiteral("(该策略无可调参数)")));
        return;
    }
    for (auto& d : defs) {
        auto* sp = new QDoubleSpinBox();
        sp->setRange(d.min, d.max);
        sp->setSingleStep(d.step > 0 ? d.step : 0.1);
        if (d.isInt) sp->setDecimals(0);
        else if (d.step < 0.01) sp->setDecimals(3);
        else if (d.step < 0.1) sp->setDecimals(2);
        else sp->setDecimals(1);
        sp->setValue(d.value);
        stratParamLayout_->addRow(QString::fromStdString(d.label), sp);
        spWidgets_.push_back(std::make_pair(d, sp));
    }
}

std::vector<StrategyParam> MainWindow::currentStrategyParams() const {
    std::vector<StrategyParam> out;
    for (auto& pr : spWidgets_) {
        StrategyParam p = pr.first;
        p.value = pr.second->value();
        out.push_back(p);
    }
    return out;
}

void MainWindow::resetStrategyParams() {
    for (auto& pr : spWidgets_) pr.second->setValue(pr.first.value);
    status_->setText(QStringLiteral("已恢复该策略的默认参数"));
}

// 参数工作室: 把界面调好的参数写回 .lua 的 params 块 (仅改 value= 令牌)
void MainWindow::saveStrategyParams() {
    if (stratCombo_->count() == 0) {
        QMessageBox::information(this, QStringLiteral("参数工作室"), QStringLiteral("没有可用策略。"));
        return;
    }
    QString luaFile = stratCombo_->currentData().toString();
    if (luaFile.isEmpty()) return;
    QString luaPath = QString::fromStdString(resolveStrategiesDir()) + "/" + luaFile;

    std::vector<StrategyParam> pv = currentStrategyParams();
    if (pv.empty()) {
        QMessageBox::information(this, QStringLiteral("参数工作室"),
            QStringLiteral("该策略没有可调参数, 无可保存内容。"));
        return;
    }
    // P0: 写前复检, 避免在已损坏的脚本上落盘
    QString err;
    if (!strategySyntaxOk(luaFile, &err)) {
        QMessageBox::critical(this, QStringLiteral("参数工作室"),
            QString("保存中止:\n当前 Lua 已有语法错误, 请先修正后重试。\n\n") + err);
        return;
    }
    int n = luaWriteParams(luaPath.toStdString(), pv);
    if (n <= 0) {
        QMessageBox::warning(this, QStringLiteral("参数工作室"),
            QString("写入失败, 参数未保存。\n\n文件: %1\n\n请确认文件未被占用或只读。").arg(luaPath));
        return;
    }
    checkStrategySyntax();
    status_->setText(QStringLiteral("✓ 已把 %1 个参数写回 %2 | 原文件已备份 %2.bak")
        .arg(n).arg(luaFile));
}

// ==================== 策略回测 (Lua) ====================
void MainWindow::runStrategyBacktest() {
    { QString _e; if (!strategySyntaxOk(stratCombo_->currentData().toString(), &_e)) { QMessageBox::information(this, QString("策略语法错误"), QString("当前策略 Lua 有语法错误, 不能运行:") + _e); return; } }
    QString text = codeEdit_->text().trimmed();
    if (text.isEmpty()) { QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请输入股票代码")); return; }
    QStringList codes = text.split(QRegularExpression(QStringLiteral("[,\\s，]+")), Qt::SkipEmptyParts);
    std::string luaFile = stratCombo_->currentData().toString().toStdString();
    if (luaFile.empty()) {
        status_->setText(QStringLiteral("未找到策略文件 (strategies/*.lua)"));
        return;
    }
    if (!luaStrategyHas(resolveStrategiesDir() + "/" + luaFile, "on_bar")) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("当前策略是筛选型(只有 on_scan), 不能做回测。\n\n"
                           "· 想选股: 点 [形态筛选 (全市场选股)]\n"
                           "· 想回测: 选择带 on_bar 的策略 (如 急杀反弹)"));
        return;
    }

    std::vector<StrategyParam> pv = currentStrategyParams();
    const std::vector<StrategyParam>* pvp = pv.empty() ? nullptr : &pv;
    std::string latestDate = engine_.latestMarketDate();
    btResults_.clear();
    for (const QString& qc : codes) {
        std::string code = normalizeCode(qc);
        if (code.empty()) continue;
        auto bars = engine_.loadSeries(code, 600);
        if (!bars.empty() && isStaleStock(bars.back().date, latestDate)) continue;   // 停牌/退市
        BacktestResult r = bt_->run(bars, luaFile, pvp);
        r.code = code;
        r.name = engine_.stockName(code);
        btResults_.push_back(std::move(r));
    }
    rightTabs_->setCurrentIndex(1);
    refreshBtTable();
    status_->setText(QStringLiteral("策略回测: 共 %1 只, 显示 %2 只 | 策略[%3] | 双击结果行看交易明细")
        .arg((int)btResults_.size()).arg(btTable_->rowCount()).arg(stratCombo_->currentText()));
}

void MainWindow::runStrategyScan() {
    { QString _e; if (!strategySyntaxOk(stratCombo_->currentData().toString(), &_e)) { QMessageBox::information(this, QString("策略语法错误"), QString("当前策略 Lua 有语法错误, 不能运行:") + _e); return; } }
    auto stocks = engine_.allStocks();
    if (stocks.empty()) { status_->setText(QStringLiteral("数据库无股票列表")); return; }
    std::string luaFile = stratCombo_->currentData().toString().toStdString();
    if (luaFile.empty()) { status_->setText(QStringLiteral("未找到策略文件")); return; }
    if (!luaStrategyHas(resolveStrategiesDir() + "/" + luaFile, "on_bar")) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("当前策略是筛选型(只有 on_scan), 不能全市场回测。\n"
                           "请选择带 on_bar 的策略, 或改用 [形态筛选] 按钮。"));
        return;
    }

    btnBt1_->setEnabled(false); btnBt2_->setEnabled(false);
    btnRun_->setEnabled(false); btnScan_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setRange(0, (int)stocks.size());

    btResults_.clear();
    btResults_.reserve(stocks.size());
    int n = (int)stocks.size();
    std::vector<StrategyParam> pv = currentStrategyParams();
    const std::vector<StrategyParam>* pvp = pv.empty() ? nullptr : &pv;
    std::string latestDate = engine_.latestMarketDate();
    int skipped = 0;
    QElapsedTimer et; et.start();
    for (int i = 0; i < n; i++) {
        auto bars = engine_.loadSeries(stocks[i].first, 600);
        if (!bars.empty() && isStaleStock(bars.back().date, latestDate)) { skipped++; continue; }   // 停牌/退市
        BacktestResult r = bt_->run(bars, luaFile, pvp);
        r.code = stocks[i].first;
        r.name = stocks[i].second;
        btResults_.push_back(std::move(r));
        if ((i & 63) == 63) {
            QApplication::processEvents();
            progress_->setValue(i + 1);
            status_->setText(QStringLiteral("全市场策略回测中: %1/%2 ...").arg(i + 1).arg(n));
        }
    }
    std::sort(btResults_.begin(), btResults_.end(),
              [](const BacktestResult& a, const BacktestResult& b) { return a.total_return > b.total_return; });
    refreshBtTable();
    progress_->setVisible(false);
    onStrategyChanged();
    btnRun_->setEnabled(true); btnScan_->setEnabled(true);
    rightTabs_->setCurrentIndex(1);
    status_->setText(QStringLiteral("全市场策略回测完成: 扫描 %1 只(跳过停牌%2), 显示 %3 只 | 耗时 %4 秒 | 双击行看明细")
        .arg(n).arg(skipped).arg(btTable_->rowCount()).arg(et.elapsed() / 1000.0, 0, 'f', 1));
}

bool MainWindow::matchBtFilters(const BacktestResult& b) const {
    if (!b.ok) return true;   // 错误行始终保留(便于排查), 但受交易数条件约束时隐藏
    if (btF1On_->isChecked() && b.n_trades < btF1Val_->value()) return false;
    if (btF2On_->isChecked() && b.total_return < btF2Val_->value()) return false;
    if (btF3On_->isChecked() && b.win_rate < btF3Val_->value()) return false;
    if (btF4On_->isChecked() && -b.max_drawdown > btF4Val_->value()) return false;
    return true;
}

void MainWindow::onBtFiltersChanged() {
    refreshBtTable();
}

void MainWindow::refreshBtTable() {
    std::vector<int> idxs;
    for (int i = 0; i < (int)btResults_.size(); i++)
        if (matchBtFilters(btResults_[i])) idxs.push_back(i);

    btTable_->setSortingEnabled(false);
    btTable_->setUpdatesEnabled(false);
    btTable_->setRowCount((int)idxs.size());
    for (int r = 0; r < (int)idxs.size(); r++) {
        const BacktestResult& b = btResults_[idxs[r]];
        auto set = [&](int col, const QString& v) {
            auto* it = new QTableWidgetItem(v);
            if (col == 0) it->setData(Qt::UserRole, idxs[r]);
            btTable_->setItem(r, col, it);
        };
        auto setN = [&](int col, const QString& v, double val) {
            auto* it = new NumericItem(v, val);
            if (col == 0) it->setData(Qt::UserRole, idxs[r]);
            btTable_->setItem(r, col, it);
        };
        set(0, QString::fromStdString(b.code));
        set(1, QString::fromStdString(b.name));
        set(2, QString::fromStdString(b.strategy_name));
        if (!b.ok) { set(9, QString::fromStdString(b.error)); continue; }
        setN(3, QString::number(b.n_trades), b.n_trades);
        setN(4, QString::asprintf("%.0f", b.win_rate), b.win_rate);
        setN(5, QString::asprintf("%+.1f", b.total_return), b.total_return);
        setN(6, QString::asprintf("%+.1f", b.worst_trade), b.worst_trade);
        setN(7, QString::asprintf("%.1f", b.max_drawdown), b.max_drawdown);
        setN(8, QString::asprintf("%.1f", b.avg_days), b.avg_days);
        if (auto* it = btTable_->item(r, 5))
            it->setForeground(b.total_return > 0 ? QColor(0x2e, 0x7d, 0x32) : QColor(0xc6, 0x28, 0x28));
    }
    btTable_->setUpdatesEnabled(true);
    btTable_->setSortingEnabled(true);
    if (idxs.size() <= 1500) btTable_->resizeColumnsToContents();

    if ((int)idxs.size() != (int)btResults_.size()) {
        status_->setText(QStringLiteral("策略回测: 显示 %1 / %2 只 (已按结果筛选过滤) | 调整筛选条即时刷新")
            .arg((int)idxs.size()).arg((int)btResults_.size()));
    }
}

void MainWindow::fillBtTable(const std::vector<BacktestResult>& rows) {
    btTable_->setSortingEnabled(false);
    btTable_->setUpdatesEnabled(false);
    btTable_->setRowCount((int)rows.size());
    for (int r = 0; r < (int)rows.size(); r++) {
        const BacktestResult& b = rows[r];
        auto set = [&](int col, const QString& v) { btTable_->setItem(r, col, new QTableWidgetItem(v)); };
        auto setN = [&](int col, const QString& v, double val) { btTable_->setItem(r, col, new NumericItem(v, val)); };
        set(0, QString::fromStdString(b.code));
        set(1, QString::fromStdString(b.name));
        set(2, QString::fromStdString(b.strategy_name));
        if (!b.ok) { set(9, QString::fromStdString(b.error)); continue; }
        setN(3, QString::number(b.n_trades), b.n_trades);
        setN(4, QString::asprintf("%.0f", b.win_rate), b.win_rate);
        setN(5, QString::asprintf("%+.1f", b.total_return), b.total_return);
        setN(6, QString::asprintf("%+.1f", b.worst_trade), b.worst_trade);
        setN(7, QString::asprintf("%.1f", b.max_drawdown), b.max_drawdown);
        setN(8, QString::asprintf("%.1f", b.avg_days), b.avg_days);
        if (auto* it = btTable_->item(r, 5))
            it->setForeground(b.total_return > 0 ? QColor(0x2e, 0x7d, 0x32) : QColor(0xc6, 0x28, 0x28));
    }
    btTable_->setUpdatesEnabled(true);
    btTable_->setSortingEnabled(true);
    if (rows.size() <= 1500) btTable_->resizeColumnsToContents();
}

void MainWindow::showTradeDetail(int row, int col) {
    (void)col;
    if (row < 0 || row >= btTable_->rowCount()) return;
    QTableWidgetItem* it0 = btTable_->item(row, 0);
    if (!it0) return;
    int idx = it0->data(Qt::UserRole).toInt();   // 行→btResults_索引(排序免疫)
    if (idx < 0 || idx >= (int)btResults_.size()) return;
    const BacktestResult& r = btResults_[idx];
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("%1 %2 — %3 | %4笔 胜率%5% 收益%6% 回撤%7%")
        .arg(QString::fromStdString(r.code), QString::fromStdString(r.name),
             QString::fromStdString(r.strategy_name))
        .arg(r.n_trades).arg(r.win_rate, 0, 'f', 0).arg(r.total_return, 0, 'f', 1)
        .arg(r.max_drawdown, 0, 'f', 1));
    auto* lay = new QVBoxLayout(&dlg);
    auto* t = new QTableWidget(&dlg);
    QStringList h = {QStringLiteral("买入日"), QStringLiteral("买入价"), QStringLiteral("卖出日"),
                     QStringLiteral("卖出价"), QStringLiteral("收益%"), QStringLiteral("持有天数"),
                     QStringLiteral("原因")};
    t->setColumnCount(h.size());
    t->setHorizontalHeaderLabels(h);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setRowCount((int)r.trades.size());
    for (int i = 0; i < (int)r.trades.size(); i++) {
        const Trade& td = r.trades[i];
        auto set = [&](int c, const QString& v) { t->setItem(i, c, new QTableWidgetItem(v)); };
        set(0, QString::fromStdString(td.buy_date));
        set(1, QString::number(td.buy_price, 'f', 2));
        set(2, QString::fromStdString(td.sell_date));
        set(3, QString::number(td.sell_price, 'f', 2));
        set(4, QString::asprintf("%+.1f", td.gain_pct));
        set(5, QString::number(td.days));
        set(6, QString::fromStdString(td.reason));
        if (auto* it = t->item(i, 4))
            it->setForeground(td.gain_pct > 0 ? QColor(0x2e, 0x7d, 0x32) : QColor(0xc6, 0x28, 0x28));
    }
    t->resizeColumnsToContents();
    lay->addWidget(t);
    dlg.resize(780, 500);
    dlg.exec();
}

// ==================== 形态筛选 (on_scan) ====================
void MainWindow::runFormScan() {
    { QString _e; if (!strategySyntaxOk(stratCombo_->currentData().toString(), &_e)) { QMessageBox::information(this, QString("策略语法错误"), QString("当前策略 Lua 有语法错误, 不能运行:") + _e); return; } }
    std::string luaFile = stratCombo_->currentData().toString().toStdString();
    if (luaFile.empty()) { status_->setText(QStringLiteral("未选择策略")); return; }
    std::string luaPath = resolveStrategiesDir() + "/" + luaFile;
    std::vector<StrategyParam> pv = currentStrategyParams();
    ScanSession sess(luaPath, pv.empty() ? nullptr : &pv);
    if (!sess.ok()) {
        QMessageBox::information(this, QStringLiteral("提示"),
            QStringLiteral("当前策略不支持全市场筛选 (缺少 on_scan 函数)\n\n") +
            QString::fromStdString(sess.error()));
        return;
    }
    auto stocks = engine_.allStocks();
    if (stocks.empty()) { status_->setText(QStringLiteral("数据库无股票列表")); return; }

    btnBt1_->setEnabled(false); btnBt2_->setEnabled(false); btnBt3_->setEnabled(false);
    btnRun_->setEnabled(false); btnScan_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setRange(0, (int)stocks.size());

    scanHits_.clear();
    int n = (int)stocks.size();
    std::string latestDate = engine_.latestMarketDate();
    int skipped = 0;
    QElapsedTimer et; et.start();

    // 全市场相对强度 RPS: 扫描前用一条 SQL 聚合算好(策略里可用 s.rps 做横截面过滤)。
    // 单只股票的 on_scan 拿不到市场分布, 所以必须在这里先算、再逐只注入。
    const int rpsDays = 120;
    std::map<std::string, double> rpsMap;
    {
        status_->setText(QStringLiteral("计算全市场相对强度 (RPS %1日) ...").arg(rpsDays));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        rpsMap = engine_.marketRps(rpsDays);
    }

    for (int i = 0; i < n; i++) {
        auto bars = engine_.loadSeries(stocks[i].first, 600);
        if (!bars.empty() && isStaleStock(bars.back().date, latestDate)) { skipped++; continue; }   // 停牌/退市
        double rps = 0.0;
        auto itR = rpsMap.find(stocks[i].first);
        if (itR != rpsMap.end()) rps = itR->second;
        auto r = sess.test(bars, rps);
        if (r.ok && r.passed) {
            ScanHitRow h;
            h.code = stocks[i].first;
            h.name = stocks[i].second;
            h.price = bars.empty() ? 0 : bars.back().close;
            h.detail = r.detail;
            scanHits_.push_back(std::move(h));
        }
        if ((i & 127) == 127) {
            QApplication::processEvents();
            progress_->setValue(i + 1);
            status_->setText(QStringLiteral("形态筛选中: %1/%2 ... 已命中 %3")
                .arg(i + 1).arg(n).arg((int)scanHits_.size()));
        }
    }
    fillScanTable();
    progress_->setVisible(false);
    onStrategyChanged();
    btnRun_->setEnabled(true); btnScan_->setEnabled(true);
    // 形态筛选已并入「四维体检」页: 切到该页并显示"形态筛选"视图
    if (resultStack_) resultStack_->setCurrentIndex(1);
    if (btnViewScan_) btnViewScan_->setChecked(true);
    if (rightTabs_) rightTabs_->setCurrentIndex(0);
    status_->setText(QStringLiteral("形态筛选[%1]: 命中 %2 / %3 只(跳过停牌%4) | RPS池 %5 只 | 耗时 %6 秒 | 双击可看备注")
        .arg(QString::fromStdString(sess.strategyName))
        .arg((int)scanHits_.size()).arg(n).arg(skipped)
        .arg((int)rpsMap.size())
        .arg(et.elapsed() / 1000.0, 0, 'f', 1));
}

void MainWindow::fillScanTable() {
    scanTable_->setSortingEnabled(false);
    scanTable_->setUpdatesEnabled(false);
    scanTable_->setRowCount((int)scanHits_.size());
    for (int r = 0; r < (int)scanHits_.size(); r++) {
        const ScanHitRow& h = scanHits_[r];
        auto set = [&](int col, const QString& v) { scanTable_->setItem(r, col, new QTableWidgetItem(v)); };
        set(0, QString::fromStdString(h.code));
        set(1, QString::fromStdString(h.name));
        scanTable_->setItem(r, 2, new NumericItem(QString::number(h.price, 'f', 2), h.price));
        set(3, QString::fromStdString(h.detail));
    }
    scanTable_->setUpdatesEnabled(true);
    scanTable_->setSortingEnabled(true);
    scanTable_->resizeColumnsToContents();
}
// fix: setUpdatesVisible -> setUpdatesEnabled

void MainWindow::runSelfTest() {
    if (!engineReady_) {
        printf("[selftest] FAIL: engine not ready\n");
        return;
    }
    printf("[selftest] stock_gui v%s\n", APP_VERSION);
    printf("[selftest] UI constructed OK (window %dx%d, min %dx%d)\n",
           width(), height(), minimumWidth(), minimumHeight());
    printf("[selftest] normalize: sz002730->%s | 002730.sz->%s | 002730->%s | SH600519->%s | bj920002->%s\n",
           normalizeCode("sz002730").c_str(), normalizeCode("002730.sz").c_str(),
           normalizeCode("002730").c_str(), normalizeCode("SH600519").c_str(),
           normalizeCode("bj920002").c_str());
    printf("[selftest] db: %s\n", resolveDbPath("kline_latest.db").c_str());
    printf("[selftest] hasHist(valuation): %s\n", engine_.hasHist() ? "yes" : "no");

    loadPool();
    runCheckup();

    int rows = table_->rowCount();
    printf("[selftest] table rows: %d\n", rows);
    for (int r = 0; r < rows && r < 6; r++) {
        QString line;
        for (int c = 0; c < table_->columnCount(); c++) {
            if (auto* it = table_->item(r, c)) {
                if (c == 1 || c == 2 || c == 3 || c == 5 || c == 11) line += it->text() + " | ";
            }
        }
        printf("[selftest]   %s\n", line.toUtf8().constData());
    }
    printf("[selftest] status: %s\n", status_->text().toUtf8().constData());

    // 全市场扫描自检
    printf("[selftest] full-market scan starting...\n");
    QElapsedTimer et2; et2.start();
    runScanAll();
    printf("[selftest] scan: cached=%d in %.1fs\n", (int)scanCache_.size(), et2.elapsed() / 1000.0);
    printf("[selftest] %s\n", status_->text().toUtf8().constData());

    // 策略回测自检 (Lua)
    printf("[selftest] strategy backtest (Lua)...\n");
    printf("[selftest] strategies dir: %s\n", resolveStrategiesDir().c_str());
    {
        auto barsTest = engine_.loadSeries("002730.SZ", 600);
        BacktestResult rb = bt_->run(barsTest, "01_dip_rebound.lua");
        printf("[selftest] bt 002730 dip_rebound: ok=%d trades=%d ret=%+.1f%% name=%s\n",
               (int)rb.ok, rb.n_trades, rb.total_return, rb.strategy_name.c_str());
        // 涨停前夜(双模式策略) 回测验证
        BacktestResult rb2 = bt_->run(barsTest, "05_next_limit_up.lua");
        printf("[selftest] bt 002730 next_limit_up: ok=%d trades=%d ret=%+.1f%% err=%s\n",
               (int)rb2.ok, rb2.n_trades, rb2.total_return, rb2.error.c_str());
        // 能力探测验证
        std::string sp = resolveStrategiesDir() + "/05_next_limit_up.lua";
        printf("[selftest] caps(05): on_bar=%d on_scan=%d\n",
               (int)luaStrategyHas(sp, "on_bar"), (int)luaStrategyHas(sp, "on_scan"));
        std::string sp1 = resolveStrategiesDir() + "/01_dip_rebound.lua";
        printf("[selftest] caps(01): on_bar=%d on_scan=%d\n",
               (int)luaStrategyHas(sp1, "on_bar"), (int)luaStrategyHas(sp1, "on_scan"));
    }
    runStrategyBacktest();
    printf("[selftest] bt table rows: %d | %s\n", btTable_->rowCount(),
           status_->text().toUtf8().constData());
    // 过滤验证: 勾选"总收益>=50%"后, 显示数应减少
    btF2On_->setChecked(true); btF2Val_->setValue(50);
    printf("[selftest] filter(ret>=50%): showing %d / %d | %s\n",
           btTable_->rowCount(), (int)btResults_.size(),
           status_->text().toUtf8().constData());
    btF2On_->setChecked(false);   // 还原

    // 形态筛选自检
    printf("[selftest] form scan (on_scan)...\n");
    {
        // 选中"涨停前夜"策略(05_)再跑筛选
        for (int i = 0; i < stratCombo_->count(); i++) {
            if (stratCombo_->itemData(i).toString().startsWith("05_")) {
                stratCombo_->setCurrentIndex(i);
                break;
            }
        }
        runFormScan();
        printf("[selftest] scan hits: %d | %s\n", (int)scanHits_.size(),
               status_->text().toUtf8().constData());
    }
    // 策略参数化验证
    {
        auto p05 = luaReadParams(resolveStrategiesDir() + "/05_next_limit_up.lua");
        auto p01 = luaReadParams(resolveStrategiesDir() + "/01_dip_rebound.lua");
        printf("[selftest] params: 05=%d个 01=%d个\n", (int)p05.size(), (int)p01.size());
        if (!p05.empty())
            printf("[selftest]   05 first: %s=%g (%s)\n", p05[0].key.c_str(), p05[0].value, p05[0].label.c_str());
        // 注入行为验证: dip 改 -3 应产生更多交易
        auto barsI = engine_.loadSeries("002730.SZ", 600);
        auto rbDef = bt_->run(barsI, "01_dip_rebound.lua");
        std::vector<StrategyParam> testP = p01;
        for (auto& pp : testP) if (pp.key == "dip") pp.value = -3;
        auto rbInj = bt_->run(barsI, "01_dip_rebound.lua", &testP);
        printf("[selftest] inject: default trades=%d | dip=-3 trades=%d\n",
               rbDef.n_trades, rbInj.n_trades);
    }

    // 扩展指标验证
    {
        Stats ds = engine_.analyze("002730.SZ", "电光科技", 0, currentParams());
        auto bars2 = engine_.loadSeries("002730.SZ", 600);
        auto chips = engine_.chipDist(bars2, ds.price);
        double pePct = engine_.pePercentile3y("002730.SZ", engine_.latestPe("002730.SZ"));
        printf("[selftest] ext: volRatio=%.2f turnover=%.1f mktcap=%.0f ret5=%+.1f ret20=%+.1f amp=%.1f fromHigh=%.1f amt5=%.2f emaRed=%d\n",
               ds.volRatio, ds.turnover, ds.mktcap, ds.ret5, ds.ret20, ds.amp,
               ds.fromHigh60, ds.amount5, ds.emaRedDays);
        printf("[selftest] chipStats: profit=%.0f%% conc70=%.1f%% conc90=%.1f%% cost50=%.2f (筛选用)\n",
               ds.chipProfit, ds.chipConc, ds.chipConc90, ds.chipCost50);
        printf("[selftest] chip: ok=%d profit=%.0f%% conc70=%.1f%% cost50=%.2f pePct=%.0f\n",
               (int)chips.ok, chips.profitRatio, chips.conc70, chips.cost50, pePct);
    }

    // 详情弹窗数据 + 板块统计 + 06策略 验证
    {
        Stats ds = engine_.analyze("002730.SZ", "电光科技", 0, currentParams());
        printf("[selftest] detail: ok=%d ma20=%.2f pos60=%.0f pb=%.2f pbPct=%.0f pe=%.1f ind=%s\n",
               (int)ds.ok, ds.ma20, ds.pos60, ds.pbCurrent, ds.pbPct3y,
               engine_.latestPe("002730.SZ"), engine_.industryOf("002730.SZ").c_str());
        int withInd = 0, withCon = 0;
        for (int r = 0; r < table_->rowCount(); r++) {
            auto* it = table_->item(r, 0);
            if (!it) continue;
            if (!engine_.industryOf(it->text().toStdString()).empty()) withInd++;
            if (!engine_.conceptsOf(it->text().toStdString()).empty()) withCon++;
        }
        printf("[selftest] sector coverage: ind %d/%d, con %d/%d\n",
               withInd, table_->rowCount(), withCon, table_->rowCount());
        std::string sp6 = resolveStrategiesDir() + "/06_hidden_accum.lua";
        printf("[selftest] caps(06): on_scan=%d\n", (int)luaStrategyHas(sp6, "on_scan"));
    }

    // K线图渲染验证: 逐一切换副图(VOL/MACD/KDJ/RSI) + BOLL, 抓帧+像素统计
    {
        KLineChart chart;
        auto bars4 = engine_.loadSeries("002730.SZ", 600);
        chart.setData(bars4, QStringLiteral("002730.SZ 电光科技"));
        chart.resize(880, 560);
        const char* subNames[7] = {"VOL", "MACD", "KDJ", "RSI", "WR", "CCI", "OBV"};
        int totalRed = 0, totalGreen = 0;
        for (int si = 0; si < 7; si++) {
            chart.setSubIndex(si);
            if (si == 3) chart.setBollOn(true);     // RSI 时顺带开 BOLL
            QPixmap pm = chart.grab();
            QImage im = pm.toImage();
            int red = 0, green = 0, white = 0, yellow = 0, purple = 0;
            for (int y = 0; y < im.height(); y += 2)
                for (int x = 0; x < im.width(); x += 2) {
                    QRgb c = im.pixel(x, y);
                    int r = qRed(c), g = qGreen(c), b = qBlue(c);
                    if (r > 170 && g < 90 && b < 90) red++;            // 红(蜡烛/MACD柱/KDJ)
                    else if (r < 110 && g > 120 && b < 110) green++;   // 绿
                    else if (r > 220 && g > 220 && b > 220) white++;   // 白(K/DIF线)
                    else if (r > 190 && g > 170 && b < 130) yellow++;  // 黄(D/DEA)
                    else if (r > 130 && g < 160 && b > 150) purple++;  // 紫(MA20/J/RSI24)
                }
            totalRed += red; totalGreen += green;
            printf("[selftest] chart[%s]: red=%d green=%d white=%d yellow=%d purple=%d\n",
                   subNames[si], red, green, white, yellow, purple);
            if (si == 3) {
                QString pngPath = QString::fromStdString(exeDir()) + "/chart_test.png";
                pm.save(pngPath);
            }
        }
        // 筹码分布图渲染验证
        {
            chart.setSubIndex(0);
            chart.setBollOn(false);
            chart.setChipsOn(true);
            QPixmap pm2 = chart.grab();
            QImage im2 = pm2.toImage();
            int red = 0, blue = 0, yellow = 0;
            for (int y = 0; y < im2.height(); y += 2)
                for (int x = 0; x < im2.width(); x += 2) {
                    QRgb c = im2.pixel(x, y);
                    int r = qRed(c), g = qGreen(c), b = qBlue(c);
                    if (r > 150 && g < 90 && b < 90) red++;
                    else if (r < 110 && g > 130 && b > 180) blue++;
                    else if (r > 190 && g > 170 && b < 130 && (r - g) < 60 && (r - g) > -60) yellow++;
                }
            printf("[selftest] chart[CHIP]: red=%d blue=%d yellow=%d (获利盘/套牢盘/现价线)\n", red, blue, yellow);
            QString png2 = QString::fromStdString(exeDir()) + "/chart_chip.png";
            pm2.save(png2);
        }
        // 周期切换验证: 周K/月K 聚合 + 渲染
        {
            chart.setPeriod(1);
            QPixmap pw = chart.grab();
            chart.setPeriod(2);
            QPixmap pm2 = chart.grab();
            chart.setPeriod(0);
            printf("[selftest] chart periods: 周K %dx%d OK, 月K %dx%d OK\n",
                   pw.width(), pw.height(), pm2.width(), pm2.height());
        }
        printf("[selftest] chart all-subviews OK (total red=%d green=%d)\n", totalRed, totalGreen);
        printf("[selftest] table columns: %d | 含筹码列=%d\n", table_->columnCount(),
               (int)(table_->columnCount() >= 24));
    }

    // 排序功能验证: 体检表按"回测%"列降序, 检查首行
    {
        table_->sortItems(5, Qt::DescendingOrder);
        if (table_->rowCount() > 0) {
            printf("[selftest] sort(bt desc) top: %s %s\n",
                   table_->item(0, 1)->text().toUtf8().constData(),
                   table_->item(0, 5)->text().toUtf8().constData());
        }
    }
    printf("[selftest] PASS\n");
}


// ==================== 新闻采集 (新闻资讯 tab) ====================
// 单 feed 同步拉取 (mirrors netclient::fetchFundFlow 模式)
QByteArray MainWindow::fetchNewsFeed(QNetworkAccessManager& mgr, const QString& url,
                                     int timeoutMs, QString* err) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/rss+xml, application/xml, text/xml, */*"));
    req.setTransferTimeout(timeoutMs);
    QNetworkReply* rep = mgr.get(req);
    QEventLoop loop;
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QByteArray body = rep->readAll();
    bool ok = (rep->error() == QNetworkReply::NoError) && !body.trimmed().isEmpty();
    if (!ok && err) *err = rep->errorString();
    rep->deleteLater();
    return body;
}

// 仅剥离 UTF-8 BOM; 默认采集源(Google News/CNBC/MarketWatch)均为 UTF-8
static QByteArray convertToUtf8(QByteArray b) {
    int n = b.size();
    if (n >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB
        && (unsigned char)b[2] == 0xBF) return b.mid(3);
    return b;
}

void MainWindow::updateNews() {
    if (!newsNM_) return;
    btnUpdateNews_->setEnabled(false);
    progress_->setVisible(true);
    int total = (int)defaultQueries().size();
    progress_->setRange(0, total);
    progress_->setValue(0);
    newsStatus_->setText(QStringLiteral("抓取新闻: 0/%1 ...").arg(total));

    QString dbPath = QString::fromStdString(resolveDbPath("news.db"));
    std::vector<NewsItem> all;
    int done = 0, okCount = 0;
    for (auto& q : defaultQueries()) {
        QString url = QString::fromStdString(buildFeedUrl(q));
        QString err;
        QByteArray body = fetchNewsFeed(*newsNM_, url, 15000, &err);
        done++;
        progress_->setValue(done);
        if (body.isEmpty()) {
            newsStatus_->setText(QStringLiteral("抓取 %1 失败: %2")
                .arg(QString::fromStdString(q.source)).arg(err));
            continue;
        }
        auto items = collectItems(std::string(convertToUtf8(body).constData(),
                                           convertToUtf8(body).size()),
                               q.source, q.category);
        if (!items.empty()) {
            okCount++;
            for (auto& it : items) all.push_back(it);
        }
        newsStatus_->setText(QStringLiteral("[%1/%2] %3: 解析 %4 条")
            .arg(done).arg(total).arg(QString::fromStdString(q.source)).arg(items.size()));
    }

    if (okCount == 0) {
        g_newsFailures++;
        progress_->setVisible(false);
        btnUpdateNews_->setEnabled(true);
        newsStatus_->setText(QStringLiteral("新闻源暂时不可用, 请稍后重试"));
        return;
    }
    g_newsFailures = 0;
    all = dedupItems(all);
    int added = cacheNews(dbPath.toStdString(), all);

    // 顺手补抓全文(离线阅读用):
    //   财联社 / 华尔街见闻的接口本身就返回完整正文, 无需抓页面
    //   (财联社详情页还是 JS 渲染的, 抓了也是空);
    //   这里只补"接口只给摘要"的新浪财经 / 东方财富。
    // "本地已经有了的就不再下载": 库里正文已达到 kEnoughLen 的条目直接跳过,
    //   所以同一条只会被下载一次; 每次只补本地还没有全文的那些, 直到补齐为止。
    //   另外给单条超时与整体耗时都设了安全阀, 失败静默跳过。
    int fetchedText = 0;
    {
        const int    kMaxFetch  = 50;      // 每次更新最多补抓 50 条
        const int    kTimeoutMs = 6000;    // 单条抓取超时
        const int    kBudgetMs  = 45000;   // 补抓阶段总耗时上限(安全阀)
        const size_t kEnoughLen = 800;     // 正文达到此长度即视为已抓到全文, 不再下载
        // 先筛出候选: 新浪/东财, 且本地还没有全文
        std::vector<NewsItem*> cand;
        for (auto& it : all) {
            if (it.source != "新浪财经" && it.source != "东方财富") continue;
            if (it.link.empty()) continue;
            if (loadNewsItem(dbPath.toStdString(), it.link).content.size() >= kEnoughLen) continue;
            cand.push_back(&it);
        }
        // 优先补最新的
        std::sort(cand.begin(), cand.end(),
                  [](const NewsItem* x, const NewsItem* y) { return x->epoch > y->epoch; });

        QElapsedTimer budget; budget.start();
        for (NewsItem* p : cand) {
            if (fetchedText >= kMaxFetch || budget.elapsed() > kBudgetMs) break;
            NewsItem& it = *p;
            newsStatus_->setText(QStringLiteral("补抓全文 %1/%2: %3")
                .arg(fetchedText + 1).arg(kMaxFetch)
                .arg(QString::fromStdString(it.title).left(20)));
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);  // 只重绘, 不收输入
            QString err;
            QByteArray html = fetchNewsFeed(*newsNM_, QString::fromStdString(it.link), kTimeoutMs, &err);
            if (html.isEmpty()) continue;
            QByteArray u8 = convertToUtf8(html);
            std::string txt = extractArticleText(std::string(u8.constData(), u8.size()));
            if (txt.size() < kEnoughLen) continue;   // 抽不出有效正文(如需 JS 渲染的页面)
            if (updateNewsContent(dbPath.toStdString(), it.link, txt) > 0) fetchedText++;
        }
    }

    loadNewsView(newsFilter_ ? newsFilter_->currentText() : QString());
    int totalNow = newsTable_ ? newsTable_->rowCount() : 0;
    progress_->setVisible(false);
    btnUpdateNews_->setEnabled(true);
    newsStatus_->setText(QStringLiteral("新闻已更新: 新增 %1 条, 补抓全文 %2 条, 缓存共 %3 条")
        .arg(added).arg(fetchedText).arg(totalNow));
}

void MainWindow::loadNewsView(const QString& category) {
    if (!newsTable_) return;
    QString dbPath = QString::fromStdString(resolveDbPath("news.db"));
    std::vector<NewsItem> rows = loadNews(dbPath.toStdString(), category.toStdString());

    QStringList hc;
    hc << QStringLiteral("时间") << QStringLiteral("来源")
       << QStringLiteral("分类") << QStringLiteral("标题");
    newsTable_->setSortingEnabled(false);
    newsTable_->setUpdatesEnabled(false);
    newsTable_->setColumnCount(4);
    newsTable_->setHorizontalHeaderLabels(hc);
    newsTable_->verticalHeader()->setVisible(false);
    newsTable_->setRowCount(0);

    for (auto& it : rows) {
        int r = newsTable_->rowCount();
        newsTable_->insertRow(r);
        QString ts = it.epoch > 0
            ? QDateTime::fromSecsSinceEpoch(it.epoch).toLocalTime()
                  .toString(QStringLiteral("yyyy-MM-dd HH:mm"))
            : QString();
        newsTable_->setItem(r, 0, new QTableWidgetItem(ts));
        newsTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(it.source)));
        newsTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(it.category)));
        auto* ti = new QTableWidgetItem(QString::fromStdString(it.title));
        ti->setData(Qt::UserRole, QString::fromStdString(it.link));
        newsTable_->setItem(r, 3, ti);
    }
    newsTable_->horizontalHeader()->setStretchLastSection(true);
    newsTable_->setUpdatesEnabled(true);
    if (rows.empty()) return;
    newsStatus_->setText(QStringLiteral("新闻缓存: 共 %1 条 (%2)")
        .arg(rows.size()).arg(category.isEmpty() ? QStringLiteral("全部") : category));
}

namespace {

QString newsHtmlEscape(const QString& s) {
    QString o = s;
    o.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    o.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    o.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return o;
}

// 把一条新闻渲染成自包含 HTML —— 内嵌浏览器视图与"导出本地 HTML 文件"共用同一份。
// 说明: 本程序链接的静态 Qt 只含 qtbase, 不含 QtWebEngine(WebEngine 在 Windows 上
// 需要 MSVC 且无法静态链接), 所以"内嵌浏览器"用的是 Qt 自带的 HTML 渲染引擎
// QTextBrowser: 支持 HTML/CSS 子集、可点击链接、Ctrl+滚轮缩放、查找, 且完全离线。
QString newsItemHtml(const NewsItem& it) {
    QString bodyText = QString::fromStdString(it.content).trimmed();
    if (bodyText.isEmpty()) bodyText = QString::fromStdString(it.description).trimmed();

    QString paras;
    const QStringList lines = bodyText.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString p = line.trimmed();
        if (!p.isEmpty()) paras += QStringLiteral("<p>%1</p>").arg(newsHtmlEscape(p));
    }
    if (paras.isEmpty())
        paras = QStringLiteral("<p class=\"muted\">(本地暂无正文) —— 点一次「抓取全文(需联网)」即可离线阅读。</p>");

    const QString when = it.epoch > 0
        ? QDateTime::fromSecsSinceEpoch(it.epoch).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        : QStringLiteral("时间未知");
    const QString link = newsHtmlEscape(QString::fromStdString(it.link));
    const QString foot = link.isEmpty()
        ? QString()
        : QStringLiteral("<div class=\"foot\">原文: <a href=\"%1\">%1</a></div>").arg(link);

    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>"
        "body{color:#222;line-height:1.75;font-family:'Microsoft YaHei',sans-serif;}"
        "h1{font-size:20px;line-height:1.4;margin:0 0 8px;}"
        ".meta{color:#888;font-size:12px;margin-bottom:16px;}"
        "p{margin:0 0 12px;font-size:14px;}"
        ".muted{color:#999;}"
        ".foot{margin-top:26px;padding-top:12px;border-top:1px solid #ddd;font-size:12px;}"
        "a{color:#0a66c2;}"
        "</style></head><body>"
        "<h1>%1</h1>"
        "<div class=\"meta\">%2 · %3 · %4 · 本地副本</div>"
        "%5%6"
        "</body></html>")
        .arg(newsHtmlEscape(QString::fromStdString(it.title)),
             newsHtmlEscape(when),
             newsHtmlEscape(QString::fromStdString(it.source)),
             newsHtmlEscape(QString::fromStdString(it.category)),
             paras,
             foot);
}

}  // namespace

void MainWindow::onNewsRowActivated(int row) {
    if (!newsTable_ || row < 0 || row >= newsTable_->rowCount()) return;
    QTableWidgetItem* t = newsTable_->item(row, 3);
    if (!t) return;
    NewsItem it;
    it.link = t->data(Qt::UserRole).toString().toStdString();
    it.title = t->text().toStdString();
    if (newsTable_->item(row, 1)) it.source = newsTable_->item(row, 1)->text().toStdString();
    if (newsTable_->item(row, 2)) it.category = newsTable_->item(row, 2)->text().toStdString();

    // 按"双击打开"选项分发 (默认 0=程序内阅读, 离线可读)
    int mode = newsOpenMode_ ? newsOpenMode_->currentData().toInt() : 0;
    if (mode == 2) {                       // 网上浏览: 打开原网页
        if (!it.link.empty()) QDesktopServices::openUrl(QUrl(QString::fromStdString(it.link)));
    } else if (mode == 1) {                // 本地浏览器: 写本地 HTML 再交给系统浏览器
        openNewsHtmlLocal(it);
    } else {                               // 程序内阅读
        showNewsReader(it);
    }
}

// 本地新闻阅读器: 正文取自 SQLite 缓存, 因此断网也能看。
// 「抓取全文」会联网拉原文页并抽取正文, 成功后写回缓存, 之后即可离线阅读。
void MainWindow::showNewsReader(const NewsItem& src) {
    QString dbPath = QString::fromStdString(resolveDbPath("news.db"));
    NewsItem it = loadNewsItem(dbPath.toStdString(), src.link);
    if (it.link.empty()) it = src;   // 库里查不到时退回列表上的信息

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("新闻阅读 — %1").arg(QString::fromStdString(it.source)));
    dlg.resize(880, 680);
    auto* lay = new QVBoxLayout(&dlg);

    auto* head = new QLabel(QString::fromStdString(it.title));
    head->setWordWrap(true);
    QFont hf = head->font();
    hf.setBold(true);
    hf.setPointSize(hf.pointSize() + 2);
    head->setFont(hf);
    lay->addWidget(head);

    QString when = it.epoch > 0
        ? QDateTime::fromSecsSinceEpoch(it.epoch).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        : QStringLiteral("时间未知");
    auto* sub = new QLabel(QStringLiteral("%1 · %2 · %3").arg(when)
        .arg(QString::fromStdString(it.source)).arg(QString::fromStdString(it.category)));
    sub->setStyleSheet(QStringLiteral("color:#777;"));
    lay->addWidget(sub);

    auto* body = new QTextBrowser();
    body->setOpenExternalLinks(true);
    lay->addWidget(body, 1);

    auto* tip = new QLabel();
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral("color:#888;"));
    lay->addWidget(tip);

    auto* row = new QHBoxLayout();
    auto* btnFetch = new QPushButton(QStringLiteral("抓取全文(需联网)"));
    auto* btnLocal = new QPushButton(QStringLiteral("导出本地HTML"));
    auto* btnOpen  = new QPushButton(QStringLiteral("网上浏览"));
    auto* btnClose = new QPushButton(QStringLiteral("关闭"));
    row->addWidget(btnFetch);
    row->addWidget(btnLocal);
    row->addWidget(btnOpen);
    row->addStretch(1);
    row->addWidget(btnClose);
    lay->addLayout(row);

    // 内嵌浏览器视图: 渲染成 HTML 交给 QTextBrowser(窗口内浏览, 点链接才走系统浏览器)
    auto render = [&]() {
        body->setHtml(newsItemHtml(it));
        const QString t = QString::fromStdString(it.content).trimmed();
        tip->setText(t.isEmpty()
            ? QStringLiteral("该条目目前只缓存了标题; 点「抓取全文(需联网)」取一次即可离线阅读。")
            : QStringLiteral("正文 %1 字, 已缓存在本地, 断网也能阅读(Ctrl+滚轮 缩放)。").arg(t.size()));
    };
    render();

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(btnLocal, &QPushButton::clicked, [&]() {
        openNewsHtmlLocal(it);   // 导出本地 HTML 文件并用系统浏览器打开(离线可看)
    });
    connect(btnOpen, &QPushButton::clicked, [&]() {
        if (!it.link.empty()) QDesktopServices::openUrl(QUrl(QString::fromStdString(it.link)));
    });
    connect(btnFetch, &QPushButton::clicked, [&]() {
        if (it.link.empty()) { tip->setText(QStringLiteral("该条目没有原文链接, 无法抓取。")); return; }
        if (!newsNM_) { tip->setText(QStringLiteral("网络模块未就绪。")); return; }
        btnFetch->setEnabled(false);
        tip->setText(QStringLiteral("正在抓取 %1 ...").arg(QString::fromStdString(it.link)));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString err;
        QByteArray html = fetchNewsFeed(*newsNM_, QString::fromStdString(it.link), 20000, &err);
        QApplication::restoreOverrideCursor();
        btnFetch->setEnabled(true);
        if (html.isEmpty()) { tip->setText(QStringLiteral("抓取失败: %1").arg(err)); return; }
        QByteArray u8 = convertToUtf8(html);
        std::string txt = extractArticleText(std::string(u8.constData(), u8.size()));
        if (txt.empty()) {
            tip->setText(QStringLiteral("这个页面抽不出正文(可能是需要 JS 渲染的站点), 建议用「网上浏览」。"));
            return;
        }
        updateNewsContent(dbPath.toStdString(), it.link, txt);
        it.content = txt;
        render();
        tip->setText(QStringLiteral("已抓取全文 %1 字并写入本地缓存, 之后断网也能阅读。").arg((int)txt.size()));
    });

    dlg.exec();
}

// 把一条新闻导出成本地 HTML 文件, 再用系统默认浏览器打开 —— 断网也能看。
// 内容与内嵌浏览器视图完全一致; 文件写在 news.db 同目录的 news_html/ 下,
// 按标题命名(同一条重复导出会覆盖更新)。
void MainWindow::openNewsHtmlLocal(const NewsItem& src) {
    QString dbPath = QString::fromStdString(resolveDbPath("news.db"));
    NewsItem it = loadNewsItem(dbPath.toStdString(), src.link);
    if (it.link.empty()) it = src;   // 库里查不到时退回列表上的信息

    // 复用与内嵌视图完全相同的 HTML, 保证"窗口里看到的"与"导出的文件"一致
    const QString html = newsItemHtml(it);

    QDir dir(QFileInfo(dbPath).absolutePath() + QStringLiteral("/news_html"));
    if (!dir.exists()) QDir().mkpath(dir.absolutePath());

    QString name = QString::fromStdString(it.title).trimmed();
    if (name.isEmpty()) name = QStringLiteral("news");
    const QString bad = QStringLiteral("\\/:*?\"<>|\r\n\t");
    for (int i = 0; i < name.size(); ++i)
        if (bad.contains(name.at(i))) name[i] = QLatin1Char('_');
    if (name.size() > 60) name = name.left(60) ;

    QString file = dir.absoluteFilePath(name + QStringLiteral(".html"));
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("本地浏览"),
            QStringLiteral("无法写入本地 HTML 文件:\n%1").arg(file));
        return;
    }
    f.write(html.toUtf8());
    f.close();
    QDesktopServices::openUrl(QUrl::fromLocalFile(file));
    if (newsStatus_) newsStatus_->setText(QStringLiteral("已生成本地副本并用浏览器打开: %1").arg(file));
}

// ==================== 实时盯盘 ====================

// 买入评分(0-100): 在现有四维指标上叠加实时价, 分数越高越偏向买入。
// 权重合计 100 —— 趋势 28 / 位置 20 / 行为 20 / 量能 16 / 估值 16,
// 最后用 Stats::verdict 封顶(下跌趋势、高位不该给高分)。权重可按需调整。
static int watchScoreOf(const Stats& s, double livePrice, double chgPct, int mode) {
    if (livePrice <= 0) livePrice = s.price;
    int sc = 0;

    // ---- 趋势(两种口径共用): 现价站上均线 + 多头排列, 满分 28 ----
    int trend = 0;
    if (s.ma5  > 0 && livePrice > s.ma5)  trend += 7;
    if (s.ma10 > 0 && livePrice > s.ma10) trend += 7;
    if (s.ma20 > 0 && livePrice > s.ma20) trend += 7;
    if (s.ma20 > 0 && s.ma5 > s.ma10 && s.ma10 > s.ma20) trend += 7;

    if (mode == 0) {
        // ================= 短线打板 / 追涨 =================
        // 趋势 30 / 位置 20(越高越强) / 强度 20(当日涨幅) / 量能 20 / 动能 10
        sc += trend * 30 / 28;

        if      (s.pos60 >= 85) sc += 20;      // 打板不吃"位置低加分", 反而要接近新高
        else if (s.pos60 >= 70) sc += 16;
        else if (s.pos60 >= 55) sc += 12;
        else if (s.pos60 >= 40) sc += 6;

        if      (chgPct >= 5.0) sc += 20;      // 当日强度
        else if (chgPct >= 3.0) sc += 16;
        else if (chgPct >= 1.0) sc += 12;
        else if (chgPct >= 0.0) sc += 8;
        else                    sc += 2;

        if      (s.volRatio >= 2.0) sc += 20;  // 量能要放出来
        else if (s.volRatio >= 1.5) sc += 15;
        else if (s.volRatio >= 1.0) sc += 10;
        else                        sc += 4;

        if      (s.emaRedDays >= 3) sc += 10;  // 连续红柱 = 动能延续
        else if (s.emaRedDays >= 1) sc += 6;
        else                        sc += 2;

        // 打板不怕高位, 只怕趋势坏; 另外无量追高压一压
        if (s.verdict.find("下跌趋势") != std::string::npos) { if (sc > 40) sc = 40; }
        else if (s.volRatio > 0 && s.volRatio < 0.8) { if (sc > 70) sc = 70; }
    } else {
        // ================= 急杀反弹 / 低吸(原口径) =================
        sc += trend;                                   // 趋势 28

        if      (s.pos60 <= 25) sc += 20;              // 位置越低越有反弹空间
        else if (s.pos60 <= 45) sc += 15;
        else if (s.pos60 <= 65) sc += 10;
        else if (s.pos60 <= 85) sc += 5;

        if      (s.fixRate >= 60) sc += 12;            // 行为 20
        else if (s.fixRate >= 45) sc += 9;
        else if (s.fixRate >= 30) sc += 6;
        else                      sc += 3;
        if (s.events  >= 5) sc += 3;
        if (s.ztCount >= 2) sc += 5;

        if      (s.volRatio >= 1.5) sc += 16;          // 量能 16
        else if (s.volRatio >= 1.2) sc += 12;
        else if (s.volRatio >= 0.8) sc += 8;
        else                        sc += 4;

        if      (s.pbPct3y < 0)    sc += 8;            // 估值 16
        else if (s.pbPct3y <= 20)  sc += 16;
        else if (s.pbPct3y <= 40)  sc += 12;
        else if (s.pbPct3y <= 60)  sc += 8;
        else if (s.pbPct3y <= 80)  sc += 4;

        if (s.verdict.find("下跌趋势") != std::string::npos) { if (sc > 45) sc = 45; }
        else if (s.verdict.find("高位") != std::string::npos) { if (sc > 60) sc = 60; }
    }

    if (sc > 100) sc = 100;
    if (sc < 0) sc = 0;
    return sc;
}

static QString watchSignalOf(int score) {
    if (score >= 80) return QStringLiteral("强烈买入");
    if (score >= 65) return QStringLiteral("买入");
    if (score >= 50) return QStringLiteral("偏多");
    if (score >= 35) return QStringLiteral("观望");
    if (score >= 20) return QStringLiteral("减仓");
    return QStringLiteral("卖出");
}

static QColor watchSignalColor(int score) {
    if (score >= 80) return QColor(0xc0, 0x28, 0x28);
    if (score >= 65) return QColor(0xd8, 0x60, 0x20);
    if (score >= 50) return QColor(0x30, 0x80, 0x30);
    if (score >= 35) return QColor(0x60, 0x60, 0x60);
    return QColor(0x20, 0x70, 0xb0);
}

// 统一盯盘代码形态: 统一成引擎认识的 "600000.SH" 形式。
// 库里 stocks.code / kline_daily.code 都是带后缀的, engine_.analyze/stockName/latestPb
// 必须传带后缀的代码; 而腾讯快照表的键也是带后缀的。所以这里一律补齐后缀,
// 不能像早先那样把后缀剥掉(那会同时打断行情查询和基线指标计算)。
static QString watchNormCode(const QString& raw) {
    QString c = raw.trimmed().toUpper();
    if (c.isEmpty()) return QString();
    const int dot = c.indexOf('.');
    const QString pure = (dot > 0) ? c.left(dot) : c;
    if (pure.size() != 6) return QString();
    for (const QChar ch : pure) if (!ch.isDigit()) return QString();
    if (dot > 0) return pure + c.mid(dot);                 // 已有后缀则保留(含 .BJ)
    const QChar h = pure.at(0);                            // 与 netclient 的 toTencentCode 同规则
    if (h == QLatin1Char('6') || h == QLatin1Char('9') || h == QLatin1Char('5'))
        return pure + QStringLiteral(".SH");
    return pure + QStringLiteral(".SZ");
}

// 从【股票代码】框导入
void MainWindow::watchImportFromCodes() {
    if (!codeEdit_) return;
    const QStringList parts = codeEdit_->text().split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                                      Qt::SkipEmptyParts);
    QStringList codes;
    for (const QString& p0 : parts) {
        const QString p = watchNormCode(p0);
        if (!p.isEmpty()) codes << p;
    }
    if (codes.isEmpty()) {
        if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 【股票代码】框里没有可识别的 6 位代码"));
        return;
    }
    watchCodes_.clear();
    for (const QString& c : codes) watchCodes_.push_back(c);
    watchRebuild();
}

// 从【四维体检】结果表导入
void MainWindow::watchImportFromTable() {
    if (!table_ || table_->rowCount() == 0) {
        if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 【四维体检】结果表还是空的, 先跑一次体检"));
        return;
    }
    watchCodes_.clear();
    for (int r = 0; r < table_->rowCount(); ++r) {
        QTableWidgetItem* it = table_->item(r, 0);
        if (!it) continue;
        const QString c = watchNormCode(it->text());
        if (!c.isEmpty()) watchCodes_.push_back(c);
    }
    watchRebuild();
}

// 只导入【四维体检】表里当前选中的行
void MainWindow::watchImportFromSelection() {
    if (!table_ || table_->rowCount() == 0) {
        if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 【四维体检】结果表还是空的, 先跑一次体检"));
        return;
    }
    const QModelIndexList sel = table_->selectionModel()
                              ? table_->selectionModel()->selectedRows() : QModelIndexList();
    if (sel.isEmpty()) {
        if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 体检表里没有选中的行(按住 Ctrl/Shift 可多选)"));
        return;
    }
    watchCodes_.clear();
    for (const QModelIndex& idx : sel) {
        QTableWidgetItem* it = table_->item(idx.row(), 0);
        if (!it) continue;
        const QString c = watchNormCode(it->text());
        if (!c.isEmpty()) watchCodes_.push_back(c);
    }
    if (watchCodes_.isEmpty()) {
        if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 选中的行里没有可识别的 6 位代码"));
        return;
    }
    watchRebuild();
}

// 重算基线四维指标(较慢: 每只都要算修复率/回测/筹码, 所以只在导入时做一次)
void MainWindow::watchRebuild() {
    if (watchCodes_.isEmpty()) { if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 没有股票")); return; }

    AnalyzeParams ap;
    if (dipSpin_)  ap.dipThreshold = dipSpin_->value();
    if (tpSpin_)   ap.takeProfit   = tpSpin_->value();
    if (holdSpin_) ap.holdDays     = holdSpin_->value();

    watchBase_.clear();
    if (btnWatchNow_) btnWatchNow_->setEnabled(false);
    const int n = watchCodes_.size();
    for (int i = 0; i < n; ++i) {
        const QString& code = watchCodes_[i];
        if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 计算基线指标 %1/%2 ...").arg(i + 1).arg(n));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const std::string cs = code.toStdString();
        const std::string nm = engine_.stockName(cs);
        const double pb = engine_.latestPb(cs);
        watchBase_.insert(code, engine_.analyze(cs, nm, pb, ap));
    }
    if (btnWatchNow_) btnWatchNow_->setEnabled(true);
    watchTick();   // 立刻出一次结果
}

// 一次实时检测: 只拉快照(快) + 用基线指标算评分
void MainWindow::watchTick() {
    if (watchCodes_.isEmpty()) {
        if (watchStatus_) watchStatus_->setText(QStringLiteral("实时盯盘: 未导入股票 (点【用股票代码框】或【用当前体检结果】)"));
        return;
    }
    QStringList codes;
    for (const QString& c : watchCodes_) codes << c;

    const int mode = watchMode_ ? watchMode_->currentData().toInt() : 0;   // 0=短线打板 1=急杀反弹
    QElapsedTimer et; et.start();
    const QVector<Quote> qs = fetchQuotes(codes);

    struct Row {
        QString code, name, sig, qtime;
        double price = 0, chg = 0, volRatio = 0, vsMa20 = 0, pos60 = 0,
               fixRate = 0, bt = 0, chip = 0;
        int score = 0;
    };
    QVector<Row> rows;
    rows.reserve(qs.size());
    for (const Quote& q : qs) {
        Stats s;
        auto it = watchBase_.constFind(q.code);
        if (it != watchBase_.constEnd()) s = it.value();
        const double live = q.price > 0 ? q.price : s.price;

        Row r;
        r.code     = q.code;
        r.name     = q.name.isEmpty() ? QString::fromStdString(s.name) : q.name;
        r.price    = q.price;
        r.chg      = q.prev > 0 ? (q.price - q.prev) / q.prev * 100.0 : 0;
        r.volRatio = s.volRatio;
        r.vsMa20   = s.ma20 > 0 ? (live - s.ma20) / s.ma20 * 100.0 : 0;
        r.pos60    = s.pos60;
        r.fixRate  = s.fixRate;
        r.bt       = s.backtest;
        r.chip     = s.chipProfit;
        r.score    = watchScoreOf(s, live, r.chg, mode);
        r.sig      = watchSignalOf(r.score);
        r.qtime    = q.time.isEmpty() ? q.date : q.time;
        rows.push_back(r);
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.score > b.score; });

    int buyCnt = 0;      // 评分>=65 的只数(供状态行使用)
    if (watchTable_) {
        watchTable_->setUpdatesEnabled(false);
        watchTable_->setRowCount(rows.size());
        for (int r = 0; r < rows.size(); ++r) {
            const Row& x = rows[r];
            auto cell = [&](int c, const QString& v) { watchTable_->setItem(r, c, new QTableWidgetItem(v)); };
            cell(0, x.code);
            cell(1, x.name);
            cell(2, QString::number(x.price, 'f', 2));
            cell(3, QStringLiteral("%1%2%").arg(x.chg >= 0 ? QStringLiteral("+") : QString())
                        .arg(QString::number(x.chg, 'f', 2)));
            cell(4, QString::number(x.volRatio, 'f', 2));
            cell(5, QStringLiteral("%1%2%").arg(x.vsMa20 >= 0 ? QStringLiteral("+") : QString())
                        .arg(QString::number(x.vsMa20, 'f', 1)));
            cell(6, QString::number(x.pos60, 'f', 0));
            cell(7, QString::number(x.fixRate, 'f', 1));
            cell(8, QString::number(x.bt, 'f', 1));
            cell(9, x.chip < 0 ? QStringLiteral("-") : QString::number(x.chip, 'f', 1));
            cell(10, QString::number(x.score));
            cell(11, x.sig);
            cell(12, x.qtime);

            // 涨跌/信号配色
            const QColor cc = x.chg > 0 ? QColor(0xc0, 0x28, 0x28)
                            : (x.chg < 0 ? QColor(0x20, 0x70, 0xb0) : QColor(0x60, 0x60, 0x60));
            if (auto* t = watchTable_->item(r, 3)) t->setForeground(cc);
            if (auto* t = watchTable_->item(r, 2)) t->setForeground(cc);
            const QColor sc = watchSignalColor(x.score);
            if (auto* t = watchTable_->item(r, 10)) { t->setForeground(sc); QFont f = t->font(); f.setBold(true); t->setFont(f); }
            if (auto* t = watchTable_->item(r, 11)) { t->setForeground(sc); QFont f = t->font(); f.setBold(true); t->setFont(f); }
            if (x.score >= 65) buyCnt++;
        }
        watchTable_->resizeColumnsToContents();
        watchTable_->setUpdatesEnabled(true);
    }

    if (watchStatus_) {
        watchStatus_->setText(QStringLiteral("实时盯盘: 共 %1 只 | 评分≥65 的 %2 只 | 上次检测 %3 | 用时 %4 秒%5")
            .arg(rows.size()).arg(buyCnt)
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
            .arg(et.elapsed() / 1000.0, 0, 'f', 1)
            .arg(rows.size() < watchCodes_.size()
                 ? QStringLiteral(" (有 %1 只没取到行情)").arg(watchCodes_.size() - rows.size())
                 : QString()));
    }
}
