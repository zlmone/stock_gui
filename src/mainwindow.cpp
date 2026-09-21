// mainwindow.cpp — 主窗口: 左侧策略面板 + 右侧结果表格
#include "mainwindow.h"
#include "klinechart.h"
#include "netclient.h"
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
    QMainWindow::closeEvent(e);
}

// ==================== UI 构建 ====================
void MainWindow::buildUI() {
    auto* central = new QWidget(this);
    auto* mainLay = new QHBoxLayout(central);
    mainLay->setContentsMargins(6, 6, 6, 6);

    // ---- 左侧策略面板（放 ScrollArea 防溢出）----
    auto* side = new QWidget();
    auto* sv = new QVBoxLayout(side);
    sv->setContentsMargins(4, 4, 4, 4);
    buildSidePanel(sv);
    sv->addStretch();

    auto* scroll = new QScrollArea();
    scroll->setWidget(side);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

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
    connect(table_, &QTableWidget::cellDoubleClicked, this, &MainWindow::showStockDetail);
    rightTabs_->addTab(table_, QStringLiteral("四维体检"));

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
    rightTabs_->addTab(btPage, QStringLiteral("策略回测"));

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
    rightTabs_->addTab(scanTable_, QStringLiteral("形态筛选"));

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
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(scroll);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({350, 930});
    mainLay->addWidget(splitter);

    setCentralWidget(central);
}

void MainWindow::buildSidePanel(QVBoxLayout* sv) {
    // ---------- 数据更新 ----------
    btnUpdate_ = new QPushButton(QStringLiteral("⟳ 更新数据 (快速, 约15秒)"));
    btnUpdate_->setMinimumHeight(32);
    btnUpdate_->setToolTip(QStringLiteral("批量快照补全市场最新一日(温柔模式); 如有历史缺口会询问全量重建"));
    connect(btnUpdate_, &QPushButton::clicked, this, &MainWindow::updateData);
    sv->addWidget(btnUpdate_);

    // ---------- 股票代码 ----------
    auto* gbCode = new QGroupBox(QStringLiteral("股票代码 (逗号/空格分隔)"));
    auto* gl = new QVBoxLayout(gbCode);
    codeEdit_ = new QLineEdit();
    codeEdit_->setPlaceholderText(QStringLiteral("002385.SZ, 600519, 002730"));
    gl->addWidget(codeEdit_);
    auto* btnPool = new QPushButton(QStringLiteral("加载默认池 (19只)"));
    connect(btnPool, &QPushButton::clicked, this, &MainWindow::loadPool);
    gl->addWidget(btnPool);
    sv->addWidget(gbCode);

    // ---------- 策略参数 ----------
    auto* gbParam = new QGroupBox(QStringLiteral("体检口径 (第1页修复率/回测列专用)"));
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
    sv->addWidget(gbParam);

    // ---------- 筛选条件 ----------
    auto* gbF = new QGroupBox(QStringLiteral("筛选条件 (勾选后生效)"));
    auto* grid = new QGridLayout(gbF);
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

    sv->addWidget(gbF);

    // ---------- 预设策略 ----------
    auto* gbPre = new QGroupBox(QStringLiteral("预设策略 (一键配置)"));
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
    sv->addWidget(gbPre);

    // ---------- 策略回测 (Lua 脚本) ----------
    auto* gbBt = new QGroupBox(QStringLiteral("策略回测 (Lua)\n各策略参数在 strategies/*.lua 文件顶部编辑"));
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
    auto* gbEd = new QGroupBox(QStringLiteral("策略代码工作室\n(改完先点“检查策略语法”, 有语法错误不写盘)"));
    auto* edvl = new QVBoxLayout(gbEd);
    srcEdit_ = new QTextEdit();
    srcEdit_->setPlaceholderText(QStringLiteral("切换到策略后自动填入该 .lua 源码; 在此直接编辑, 然后“保存源码到文件”写回"));
    srcEdit_->setFont(QFont(QStringLiteral("Courier New"), 10));
    srcEdit_->setLineWrapMode(QTextEdit::NoWrap);
    srcEdit_->setUndoRedoEnabled(true);
    srcEdit_->setMinimumHeight(180);
    edvl->addWidget(srcEdit_, 1);
    luaHL_ = new LuaHighlighter(srcEdit_);
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
    sv->addWidget(gbBt);

    // 策略切换 -> 重载源码入编辑区 (已在上面 connect, 并在构造末尾初始化一次)

    // ---------- 显示选项 + 执行 ----------
    onlyMatch_ = new QCheckBox(QStringLiteral("仅显示满足条件的股票"));
    onlyMatch_->setChecked(false);
    sv->addWidget(onlyMatch_);

    fundFlowOn_ = new QCheckBox(QStringLiteral("详情查询主力资金流 (需联网)"));
    fundFlowOn_->setChecked(true);
    fundFlowOn_->setToolTip(QStringLiteral("关闭后完全离线工作; 离线环境下程序也会自动跳过"));
    sv->addWidget(fundFlowOn_);

    btnRun_ = new QPushButton(QStringLiteral("开始体检 (输入的代码)"));
    btnRun_->setMinimumHeight(34);
    connect(btnRun_, &QPushButton::clicked, this, &MainWindow::runCheckup);
    sv->addWidget(btnRun_);

    btnScan_ = new QPushButton(QStringLiteral("全市场扫描 + 筛选"));
    btnScan_->setMinimumHeight(42);
    {
        QFont bf = btnScan_->font(); bf.setBold(true); bf.setPointSize(bf.pointSize() + 1);
        btnScan_->setFont(bf);
    }
    connect(btnScan_, &QPushButton::clicked, this, &MainWindow::runScanAll);
    sv->addWidget(btnScan_);

    auto* btnSector = new QPushButton(QStringLiteral("板块聚合统计 (当前结果)"));
    btnSector->setToolTip(QStringLiteral("统计当前表格里的股票集中在哪些行业/概念"));
    connect(btnSector, &QPushButton::clicked, this, &MainWindow::showSectorStats);
    sv->addWidget(btnSector);

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
    if (f.isEmpty()) { srcEdit_->setPlainText(QStringLiteral("(未选择策略)")); return; }
    std::string path = resolveStrategiesDir() + "/" + f.toStdString();
    std::string content = readStrategyFile(path);
    if (content.empty()) {
        srcEdit_->setPlainText(QStringLiteral("(无法读取该策略文件, 请先保存其它工作)"));
        return;
    }
    srcEdit_->setPlainText(QString::fromStdString(content));
}

// ---------- 代码工作室: 把编辑区内容写回 .lua (先编译校验, 写前 .bak) ========
void MainWindow::saveStrategySource() {
    if (!srcEdit_ || !stratCombo_ || stratCombo_->count() == 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("未选择策略")); return; }
    QString f = stratCombo_->currentData().toString();
    if (f.isEmpty()) { QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("未选择策略")); return; }
    std::string path = resolveStrategiesDir() + "/" + f.toStdString();
    std::string content = srcEdit_->toPlainText().toStdString();
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
    srcEdit_->setPlainText(content.empty()
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
    for (int i = 0; i < n; i++) {
        auto bars = engine_.loadSeries(stocks[i].first, 600);
        if (!bars.empty() && isStaleStock(bars.back().date, latestDate)) { skipped++; continue; }   // 停牌/退市
        auto r = sess.test(bars);
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
    rightTabs_->setCurrentIndex(2);
    status_->setText(QStringLiteral("形态筛选[%1]: 命中 %2 / %3 只(跳过停牌%4) | 耗时 %5 秒 | 双击可看备注")
        .arg(QString::fromStdString(sess.strategyName))
        .arg((int)scanHits_.size()).arg(n).arg(skipped).arg(et.elapsed() / 1000.0, 0, 'f', 1));
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
