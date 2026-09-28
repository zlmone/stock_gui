// klinechart.cpp — K线图控件 v2 实现
#include "klinechart.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFontMetrics>
#include <cmath>
#include <limits>
#include <algorithm>

static const double kNaN = std::numeric_limits<double>::quiet_NaN();

// ==================== 构造: 控制栏 ====================
KLineChart::KLineChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(560, 460);

    ctrlBar_ = new QWidget(this);
    auto* hb = new QHBoxLayout(ctrlBar_);
    hb->setContentsMargins(4, 2, 4, 2);
    hb->setSpacing(8);
    auto mkCb = [&](const char* txt, bool checked) {
        auto* cb = new QCheckBox(QString::fromUtf8(txt), ctrlBar_);
        cb->setChecked(checked);
        connect(cb, &QCheckBox::toggled, this, [this]() { update(); });
        hb->addWidget(cb);
        return cb;
    };
    cbMA5_   = mkCb("MA5", true);
    cbMA10_  = mkCb("MA10", true);
    cbMA20_  = mkCb("MA20", true);
    cbMA60_  = mkCb("MA60", true);
    cbMA120_ = mkCb("MA120", false);
    cbMA250_ = mkCb("年线", false);
    cbBOLL_  = mkCb("BOLL", false);
    cbChips_ = mkCb("筹码", false);
    hb->addSpacing(10);
    auto* lb = new QLabel(QStringLiteral("副图:"), ctrlBar_);
    hb->addWidget(lb);
    subCombo_ = new QComboBox(ctrlBar_);
    subCombo_->addItems({QStringLiteral("成交量"), QStringLiteral("MACD"),
                         QStringLiteral("KDJ"), QStringLiteral("RSI"),
                         QStringLiteral("WR"), QStringLiteral("CCI"), QStringLiteral("OBV")});
    connect(subCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { update(); });
    hb->addWidget(subCombo_);
    subCombo_->setCurrentIndex(1);   // 默认 MACD
    hb->addSpacing(8);
    periodCombo_ = new QComboBox(ctrlBar_);
    periodCombo_->addItems({QStringLiteral("日K"), QStringLiteral("周K"), QStringLiteral("月K")});
    connect(periodCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &KLineChart::setPeriod);
    hb->addWidget(periodCombo_);
    hb->addSpacing(6);
    auto* btnZoomOut = new QPushButton("-", ctrlBar_);
    btnZoomOut->setFixedSize(28, 18);
    btnZoomOut->setToolTip(QStringLiteral("缩小: 显示更多K线"));
    connect(btnZoomOut, &QPushButton::clicked, this, &KLineChart::zoomOut);
    hb->addWidget(btnZoomOut);
    zoomLabel_ = new QLabel("1.0x", ctrlBar_);
    zoomLabel_->setMinimumWidth(40);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    hb->addWidget(zoomLabel_);
    auto* btnZoomIn = new QPushButton("+", ctrlBar_);
    btnZoomIn->setFixedSize(28, 18);
    btnZoomIn->setToolTip(QStringLiteral("放大: 显示更少K线, 文字同步变大"));
    connect(btnZoomIn, &QPushButton::clicked, this, &KLineChart::zoomIn);
    hb->addWidget(btnZoomIn);
    hb->addStretch();
    ctrlBar_->setStyleSheet(QStringLiteral(
        "QCheckBox{color:#cfd4da;font-size:11px;}"
        "QLabel{color:#cfd4da;font-size:11px;}"
        "QComboBox{font-size:11px;}"));
    ctrlBar_->setGeometry(0, 0, width(), 26);
}

void KLineChart::resizeEvent(QResizeEvent*) {
    if (ctrlBar_) ctrlBar_->setGeometry(0, 0, width(), ctrlBar_->sizeHint().height());
}

void KLineChart::zoomIn() {
    int n = (int)bars_.size();
    if (n < 5) return;
    showDays_ = std::max(30, std::min((int)std::min(500, n), (int)(showDays_ / 1.25)));
    update();
}

void KLineChart::zoomOut() {
    int n = (int)bars_.size();
    if (n < 5) return;
    showDays_ = std::min((int)std::min(500, n), (int)(showDays_ * 1.25));
    update();
}

double KLineChart::zoomScale() const {
    const double zMin = 30.0, zMax = 500.0;
    double t = (showDays_ - zMin) / (zMax - zMin);
    if (t < 0) t = 0; if (t > 1) t = 1;
    return 1.0 + (1.0 - t) * 1.2;
}

void KLineChart::setSubIndex(int idx) {
    if (subCombo_) subCombo_->setCurrentIndex(idx);
    update();
}

void KLineChart::setBollOn(bool on) {
    if (cbBOLL_) cbBOLL_->setChecked(on);
    update();
}

void KLineChart::setChipsOn(bool on) {
    if (cbChips_) cbChips_->setChecked(on);
    update();
}

// 筹码分布 (三角分布法, 近260根, 与引擎口径一致)
void KLineChart::computeChips() {
    chips_.clear();
    chipLo_ = chipHi_ = 0;
    chipProfit_ = -1;
    size_t n = bars_.size();
    if (n < 20) return;
    size_t start = (n > 260) ? n - 260 : 0;
    double lo = 1e18, hi = 0;
    for (size_t i = start; i < n; i++) {
        lo = std::min(lo, bars_[i].low);
        hi = std::max(hi, bars_[i].high);
    }
    if (hi <= lo) return;
    const int baskets = 120;
    double step = (hi - lo) / baskets;
    chips_.assign(baskets, 0.0);
    for (size_t i = start; i < n; i++) {
        const Bar& b = bars_[i];
        if (b.volume <= 0) continue;
        double typ = (b.open + b.close + b.high + b.low) / 4.0;
        int kLo = std::max(0, std::min(baskets - 1, (int)((b.low - lo) / step)));
        int kHi = std::max(0, std::min(baskets - 1, (int)((b.high - lo) / step)));
        double wsum = 0;
        std::vector<double> w(kHi - kLo + 1, 0.0);
        for (int k = kLo; k <= kHi; k++) {
            double p = lo + (k + 0.5) * step;
            double d;
            if (p <= typ) d = (typ > b.low) ? (p - b.low) / (typ - b.low) : 1.0;
            else          d = (b.high > typ) ? (b.high - p) / (b.high - typ) : 1.0;
            w[k - kLo] = std::max(d, 0.0);
            wsum += w[k - kLo];
        }
        if (wsum <= 0) {
            int k = std::max(0, std::min(baskets - 1, (int)((b.close - lo) / step)));
            chips_[k] += b.volume;
            continue;
        }
        for (int k = kLo; k <= kHi; k++)
            chips_[k] += w[k - kLo] * (b.volume / wsum);
    }
    chipLo_ = lo;
    chipHi_ = hi;
    double below = 0, total = 0;
    double price = bars_[n - 1].close;
    for (int k = 0; k < baskets; k++) {
        total += chips_[k];
        if (lo + (k + 0.5) * step < price) below += chips_[k];
    }
    chipProfit_ = total > 0 ? below / total * 100.0 : 0.0;
    // 70%/90% 集中度 = (高成本端-低成本端)/(两者之和)
    auto costAt = [&](double pct) -> double {
        if (total <= 0) return 0.0;
        double target = total * pct / 100.0, acc = 0;
        for (int k = 0; k < baskets; k++) {
            acc += chips_[k];
            if (acc >= target) return lo + (k + 0.5) * step;
        }
        return hi;
    };
    double c5 = costAt(5), c15 = costAt(15), c85 = costAt(85), c95 = costAt(95);
    chipConc70_ = (c85 + c15) > 0 ? (c85 - c15) / (c85 + c15) * 100.0 : 0.0;
    chipConc90_ = (c95 + c5) > 0 ? (c95 - c5) / (c95 + c5) * 100.0 : 0.0;
}

QRect KLineChart::canvasRect() const {
    int top = ctrlBar_ ? ctrlBar_->height() : 0;
    return rect().adjusted(0, top, 0, 0);
}

// ==================== 周期聚合 (日线 -> 周/月) ====================
static long dayOrdinal(const std::string& d) {   // "2026-09-18" -> 天序数
    if (d.size() < 10) return -1;
    int y = atoi(d.substr(0, 4).c_str());
    int m = atoi(d.substr(5, 2).c_str());
    int dd = atoi(d.substr(8, 2).c_str());
    if (m <= 2) { y--; m += 12; }
    return 365L * y + y / 4 - y / 100 + y / 400 + (153L * (m - 3) + 2) / 5 + dd;
}

static std::vector<Bar> aggregateBars(const std::vector<Bar>& daily, int period) {
    std::vector<Bar> out;
    long curKey = LONG_MIN;
    Bar cur;
    for (const Bar& b : daily) {
        long dn = dayOrdinal(b.date);
        if (dn < 0) continue;
        long key;
        if (period == 2) {   // 月: y*12+m
            int y = atoi(b.date.substr(0, 4).c_str());
            int m = atoi(b.date.substr(5, 2).c_str());
            key = y * 12L + m;
        } else {             // 周: 天序数/7
            key = dn / 7;
        }
        if (key != curKey) {
            if (curKey != LONG_MIN) out.push_back(cur);
            cur = b;
            curKey = key;
        } else {
            cur.high = std::max(cur.high, b.high);
            cur.low = std::min(cur.low, b.low);
            cur.close = b.close;
            cur.volume += b.volume;
            cur.date = b.date;
        }
    }
    if (curKey != LONG_MIN) out.push_back(cur);
    return out;
}

void KLineChart::setPeriod(int p) {
    period_ = p;
    bars_ = (p == 0) ? rawBars_ : aggregateBars(rawBars_, p);
    computeAll();
    computeChips();
    showDays_ = (p == 0) ? 120 : (p == 1 ? 104 : 60);
    hoverIdx_ = -1;
    update();
}

// ==================== 数据 + 全指标计算 ====================
void KLineChart::setData(const std::vector<Bar>& bars, const QString& title) {
    rawBars_ = bars;
    bars_ = bars;
    period_ = 0;
    title_ = title;
    computeAll();
    computeChips();
    hoverIdx_ = -1;
    update();
}

void KLineChart::computeAll() {
    size_t n = bars_.size();
    auto ma = [&](int m, std::vector<double>& out) {
        out.assign(n, 0.0);
        double sum = 0;
        for (size_t i = 0; i < n; i++) {
            sum += bars_[i].close;
            if ((int)i >= m) sum -= bars_[i - m].close;
            out[i] = ((int)i >= m - 1) ? sum / m : kNaN;
        }
    };
    ma(5, ma5_); ma(10, ma10_); ma(20, ma20_); ma(60, ma60_); ma(120, ma120_); ma(250, ma250_);

    // BOLL(20, 2)
    bollMid_.assign(n, kNaN); bollUp_.assign(n, kNaN); bollLow_.assign(n, kNaN);
    for (size_t i = 19; i < n; i++) {
        double mean = 0;
        for (size_t k = i - 19; k <= i; k++) mean += bars_[k].close;
        mean /= 20.0;
        double var = 0;
        for (size_t k = i - 19; k <= i; k++) var += (bars_[k].close - mean) * (bars_[k].close - mean);
        double sd = std::sqrt(var / 20.0);
        bollMid_[i] = mean;
        bollUp_[i] = mean + 2.0 * sd;
        bollLow_[i] = mean - 2.0 * sd;
    }

    // MACD(12,26,9)
    dif_.assign(n, 0.0); dea_.assign(n, 0.0); macd_.assign(n, 0.0);
    {
        const double a12 = 2.0 / 13.0, a26 = 2.0 / 27.0, a9 = 2.0 / 10.0;
        double e12 = 0, e26 = 0, dea = 0;
        for (size_t i = 0; i < n; i++) {
            double c = bars_[i].close;
            e12 = (i == 0) ? c : a12 * c + (1 - a12) * e12;
            e26 = (i == 0) ? c : a26 * c + (1 - a26) * e26;
            dif_[i] = e12 - e26;
            dea = (i == 0) ? dif_[i] : a9 * dif_[i] + (1 - a9) * dea;
            dea_[i] = dea;
            macd_[i] = (dif_[i] - dea) * 2.0;
        }
    }

    // KDJ(9,3,3)
    k_.assign(n, kNaN); d_.assign(n, kNaN); j_.assign(n, kNaN);
    for (size_t i = 8; i < n; i++) {
        double hi = bars_[i - 8].high, lo = bars_[i - 8].low;
        for (size_t q = i - 8; q <= i; q++) {
            hi = std::max(hi, bars_[q].high);
            lo = std::min(lo, bars_[q].low);
        }
        double rsv = (hi > lo) ? (bars_[i].close - lo) / (hi - lo) * 100.0 : 50.0;
        k_[i] = (i == 8) ? (2.0 / 3.0 * 50 + 1.0 / 3.0 * rsv)
                         : (2.0 / 3.0 * k_[i - 1] + 1.0 / 3.0 * rsv);
        d_[i] = (i == 8) ? (2.0 / 3.0 * 50 + 1.0 / 3.0 * k_[i])
                         : (2.0 / 3.0 * d_[i - 1] + 1.0 / 3.0 * k_[i]);
        j_[i] = 3.0 * k_[i] - 2.0 * d_[i];
    }

    // RSI(6,12,24) Wilder 平滑
    auto rsi = [&](int period, std::vector<double>& out) {
        out.assign(n, kNaN);
        double ag = 0, al = 0;
        for (size_t i = 1; i < n; i++) {
            double chg = bars_[i].close - bars_[i - 1].close;
            double g = std::max(chg, 0.0), l = std::max(-chg, 0.0);
            if ((int)i <= period) {
                ag = (ag * (i - 1) + g) / i;
                al = (al * (i - 1) + l) / i;
            } else {
                ag = (ag * (period - 1) + g) / period;
                al = (al * (period - 1) + l) / period;
            }
            out[i] = (al <= 1e-12) ? 100.0 : 100.0 - 100.0 / (1.0 + ag / al);
        }
        // 前 period 根保持 NaN
    };
    rsi(6, rsi6_); rsi(12, rsi12_); rsi(24, rsi24_);

    // WR(14) 威廉指标: (H14 - C) / (H14 - L14) * 100  (0~100)
    wr14_.assign(n, kNaN);
    for (size_t i = 13; i < n; i++) {
        double hi = bars_[i].high, lo = bars_[i].low;
        for (size_t k = i - 13; k <= i; k++) {
            hi = std::max(hi, bars_[k].high);
            lo = std::min(lo, bars_[k].low);
        }
        wr14_[i] = (hi > lo) ? (hi - bars_[i].close) / (hi - lo) * 100.0 : 50.0;
    }

    // CCI(14): (TP - MA(TP,14)) / (0.015 * MD)
    cci14_.assign(n, kNaN);
    {
        std::vector<double> tp(n, 0);
        for (size_t i = 0; i < n; i++)
            tp[i] = (bars_[i].high + bars_[i].low + bars_[i].close) / 3.0;
        for (size_t i = 13; i < n; i++) {
            double ma = 0;
            for (size_t k = i - 13; k <= i; k++) ma += tp[k];
            ma /= 14.0;
            double md = 0;
            for (size_t k = i - 13; k <= i; k++) md += std::fabs(tp[k] - ma);
            md /= 14.0;
            cci14_[i] = (md > 1e-9) ? (tp[i] - ma) / (0.015 * md) : 0.0;
        }
    }

    // OBV 能量潮: 涨日累计+成交量, 跌日-
    obv_.assign(n, 0.0);
    for (size_t i = 1; i < n; i++) {
        double v = (bars_[i].close > bars_[i-1].close) ? bars_[i].volume
                 : (bars_[i].close < bars_[i-1].close) ? -bars_[i].volume : 0.0;
        obv_[i] = obv_[i-1] + v;
    }
}

// ==================== 绘制 ====================
void KLineChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x14, 0x17, 0x1c));

    int n = (int)bars_.size();
    if (n < 5) {
        p.setPen(QColor(0xbb, 0xbb, 0xbb));
        p.drawText(canvasRect(), Qt::AlignCenter, QStringLiteral("无K线数据"));
        return;
    }
    int vis = std::min(showDays_, n);
    int start = n - vis;

    QRect cr = canvasRect();
    double zs = zoomScale();
    if (zoomLabel_) zoomLabel_->setText(QString("%1x").arg(zoomScale(), 0, 'f', 1));
    QFont f(QFont(QStringLiteral("Consolas"), 0));
    f.setPixelSize(qMax(9, int(9 * zs)));
    QFontMetrics pf(f);
    const int mL = 10;
    const int mR = qMax(70, int(30 * zs) + 30);
    const int mB = qMax(16, int(15 * zs) + 2);
    const int mT = qMax(20, int(22 * zs) + 2);
    const int labelH = qMax(18, int(16 * zs) + 4);
    bool showChips = (cbChips_ && cbChips_->isChecked() && !chips_.empty() && chipHi_ > chipLo_);
    int chipW = showChips ? 150 : 0;
    int chartW = cr.width() - mL - mR - chipW;
    int totalH = cr.height() - mT - mB;
    if (chartW < 50 || totalH < 120) return;
    int mainH = (int)(totalH * 0.70);
    int subH = totalH - mainH - 8;
    QRect mainR(cr.left() + mL, cr.top() + mT, chartW, mainH);
    QRect subR(cr.left() + mL, cr.top() + mT + mainH + 8, chartW, std::max(30, subH));
    QRect chipR(mainR.left() + chartW + 2, mainR.top(), chipW - 6, mainR.height());

    // ---------- 价格范围 (含勾选的 BOLL) ----------
    double lo = 1e18, hi = 0;
    for (int i = start; i < n; i++) {
        lo = std::min(lo, bars_[i].low);
        hi = std::max(hi, bars_[i].high);
    }
    if (cbBOLL_->isChecked()) {
        for (int i = start; i < n; i++) {
            if (!std::isnan(bollUp_[i]))  hi = std::max(hi, bollUp_[i]);
            if (!std::isnan(bollLow_[i])) lo = std::min(lo, bollLow_[i]);
        }
    }
    if (hi <= lo) hi = lo + 1;
    double pad = (hi - lo) * 0.05;
    lo -= pad; hi += pad;
    auto yOf = [&](double price) {
        return mainR.top() + (hi - price) / (hi - lo) * mainR.height();
    };
    double binW = (double)chartW / vis;
    auto xOf = [&](int k) { return mainR.left() + (k + 0.5) * binW; };

    // ---------- 网格 + 价格刻度 ----------
    p.setFont(f);
    for (int g = 0; g <= 4; g++) {
        double price = lo + (hi - lo) * g / 4.0;
        int y = (int)yOf(price);
        p.setPen(QPen(QColor(0x25, 0x2a, 0x31), 1, Qt::DotLine));
        p.drawLine(mainR.left(), y, mainR.right(), y);
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(mainR.right() + 4, y - labelH / 2, mR - 6, labelH),
                   Qt::AlignVCenter | Qt::AlignLeft, QString::number(price, 'f', 2));
    }

    // ---------- 蜡烛 ----------
    int bodyW = std::max(2, (int)(binW * 0.66));
    for (int k = 0; k < vis; k++) {
        int i = start + k;
        const Bar& b = bars_[i];
        bool up = b.close >= b.open;
        QColor c = up ? QColor(0xe5, 0x39, 0x35) : QColor(0x43, 0xa0, 0x47);
        p.setPen(c);
        int x = (int)xOf(k);
        p.drawLine(x, (int)yOf(b.high), x, (int)yOf(b.low));
        int yO = (int)yOf(b.open), yC = (int)yOf(b.close);
        int top = std::min(yO, yC), bot = std::max(yO, yC);
        if (bot - top < 1) bot = top + 1;
        p.fillRect(QRect(x - bodyW / 2, top, bodyW, bot - top), c);
    }

    // ---------- 主图叠加线 ----------
    auto drawSeries = [&](const std::vector<double>& arr, const QColor& c,
                          qreal w = 1.0, Qt::PenStyle style = Qt::SolidLine) {
        p.setPen(QPen(c, w, style));
        QPointF prev; bool has = false;
        for (int k = 0; k < vis; k++) {
            int i = start + k;
            if (i >= (int)arr.size() || std::isnan(arr[i])) { has = false; continue; }
            QPointF pt(xOf(k), yOf(arr[i]));
            if (has) p.drawLine(prev, pt);
            prev = pt; has = true;
        }
    };
    if (cbMA5_->isChecked())   drawSeries(ma5_,   QColor(0xff, 0xd5, 0x4f));
    if (cbMA10_->isChecked())  drawSeries(ma10_,  QColor(0x4f, 0xc3, 0xf7));
    if (cbMA20_->isChecked())  drawSeries(ma20_,  QColor(0xba, 0x68, 0xc8));
    if (cbMA60_->isChecked())  drawSeries(ma60_,  QColor(0x81, 0xc7, 0x84));
    if (cbMA120_->isChecked()) drawSeries(ma120_, QColor(0xff, 0x8a, 0x50));
    if (cbMA250_->isChecked()) drawSeries(ma250_, QColor(0xff, 0x69, 0xb4));
    if (cbBOLL_->isChecked()) {
        drawSeries(bollUp_,  QColor(0x64, 0xb5, 0xf6), 1.0, Qt::DashLine);
        drawSeries(bollMid_, QColor(0xd0, 0xd0, 0xd0), 1.0, Qt::DashLine);
        drawSeries(bollLow_, QColor(0x64, 0xb5, 0xf6), 1.0, Qt::DashLine);
    }

    // ---------- 筹码分布 (图形) ----------
    if (showChips) {
        double cmax = 0;
        for (double v : chips_) cmax = std::max(cmax, v);
        int baskets = (int)chips_.size();
        double cstep = (chipHi_ - chipLo_) / baskets;
        double priceNow = bars_[n - 1].close;
        int barH = std::max(1, (int)(mainR.height() / (chipHi_ - chipLo_) * cstep) - 1);
        for (int k = 0; k < baskets; k++) {
            double p2 = chipLo_ + (k + 0.5) * cstep;
            int y = (int)yOf(p2);
            if (y < mainR.top() - barH || y > mainR.bottom()) continue;
            int w = cmax > 0 ? (int)(chips_[k] / cmax * (chipW - 14)) : 0;
            if (w < 1) w = 1;
            QColor c = (p2 < priceNow) ? QColor(0xe5, 0x39, 0x35, 170)    // 获利盘(现价下方)
                                       : QColor(0x4f, 0xc3, 0xf7, 160);   // 套牢盘(现价上方)
            p.fillRect(QRect(chipR.left(), y - barH / 2, w, barH), c);
        }
        // 现价虚线
        int yNow = (int)yOf(priceNow);
        p.setPen(QPen(QColor(0xff, 0xd5, 0x4f), 1, Qt::DashLine));
        p.drawLine(chipR.left(), yNow, chipR.left() + chipW - 10, yNow);
        // 标注
        p.setPen(QColor(0xcf, 0xd4, 0xda));
        p.drawText(QRect(chipR.left(), mainR.top() - 16, chipW + 60, 14), Qt::AlignLeft,
                   QString("筹码峰 获利%1%  红获 蓝套").arg(chipProfit_, 0, 'f', 0));
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(chipR.left(), mainR.bottom() + 2, chipW + 60, 14), Qt::AlignLeft,
                   QString("集中70 %1%  90 %2%  越小越集中").arg(chipConc70_, 0, 'f', 1).arg(chipConc90_, 0, 'f', 1));
    }

    // ---------- 副图 ----------
    int sub = subCombo_ ? subCombo_->currentIndex() : 0;
    p.setFont(QFont(QStringLiteral("Consolas"), 8));
    if (sub == 0) {
        // 成交量
        double vmax = 0;
        for (int i = start; i < n; i++) vmax = std::max(vmax, bars_[i].volume);
        for (int k = 0; k < vis; k++) {
            int i = start + k;
            bool up = bars_[i].close >= bars_[i].open;
            QColor c = up ? QColor(0xb0, 0x2e, 0x2a) : QColor(0x2e, 0x7d, 0x32);
            int x = (int)xOf(k);
            int h = vmax > 0 ? (int)(bars_[i].volume / vmax * (subR.height() - 6)) : 0;
            p.fillRect(QRect(x - bodyW / 2, subR.bottom() - h, bodyW, h), c);
        }
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(subR.left(), subR.top() - 3, 200, 14), Qt::AlignLeft,
                   QString("VOL %1万手").arg(vmax / 1e4, 0, 'f', 0));
    } else if (sub == 1) {
        // MACD
        double vmax = 1e-9;
        for (int i = start; i < n; i++) {
            vmax = std::max(vmax, std::fabs(macd_[i]));
            vmax = std::max(vmax, std::fabs(dif_[i]));
            vmax = std::max(vmax, std::fabs(dea_[i]));
        }
        vmax *= 1.1;
        double y0 = subR.center().y();
        auto yM = [&](double v) { return y0 - v / vmax * (subR.height() / 2 - 2); };
        // 零轴
        p.setPen(QPen(QColor(0x40, 0x45, 0x50), 1, Qt::DotLine));
        p.drawLine(subR.left(), (int)y0, subR.right(), (int)y0);
        // 柱
        for (int k = 0; k < vis; k++) {
            int i = start + k;
            QColor c = macd_[i] >= 0 ? QColor(0xc6, 0x28, 0x28) : QColor(0x2e, 0x7d, 0x32);
            p.setPen(c);
            int x = (int)xOf(k);
            double yv = yM(macd_[i]);
            int top = std::min((int)y0, (int)yv), bot = std::max((int)y0, (int)yv);
            if (bot - top < 1) bot = top + 1;
            p.fillRect(QRect(x - bodyW / 2, top, bodyW, bot - top), c);
        }
        // DIF / DEA
        auto drawSubLine = [&](const std::vector<double>& arr, const QColor& c) {
            p.setPen(QPen(c, 1));
            QPointF prev; bool has = false;
            for (int k = 0; k < vis; k++) {
                int i = start + k;
                if (i >= (int)arr.size() || std::isnan(arr[i])) { has = false; continue; }
                QPointF pt(xOf(k), yM(arr[i]));
                if (has) p.drawLine(prev, pt);
                prev = pt; has = true;
            }
        };
        drawSubLine(dif_, QColor(0xff, 0xff, 0xff));
        drawSubLine(dea_, QColor(0xff, 0xd5, 0x4f));
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(subR.left(), subR.top() - 3, qMax(260, (int)(260 * zs) + 40), labelH), Qt::AlignLeft,
                   QStringLiteral("MACD(12,26,9)  DIF 白  DEA 黄"));
        p.drawText(QRect(subR.right() + 4, (int)y0 - labelH / 2, mR - 6, labelH),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("0"));
        p.drawText(QRect(subR.right() + 4, subR.top(), mR - 6, labelH),
                   Qt::AlignLeft, QString::number(vmax, 'f', 2));
        p.drawText(QRect(subR.right() + 4, subR.bottom() - 14, mR - 6, labelH),
                   Qt::AlignLeft, QString::number(-vmax, 'f', 2));
    } else if (sub == 2) {
        // KDJ: 范围 [-20, 120]
        auto yK = [&](double v) {
            double t = (v + 20.0) / 140.0;
            return subR.bottom() - t * subR.height();
        };
        p.setPen(QPen(QColor(0x35, 0x3a, 0x45), 1, Qt::DotLine));
        for (double lv : {0.0, 50.0, 100.0}) {
            int y = (int)yK(lv);
            p.drawLine(subR.left(), y, subR.right(), y);
        }
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        for (double lv : {0.0, 50.0, 100.0}) {
            p.drawText(QRect(subR.right() + 4, (int)yK(lv) - labelH / 2, mR - 6, labelH),
                       Qt::AlignLeft | Qt::AlignVCenter, QString::number((int)lv));
        }
        auto drawSubLine = [&](const std::vector<double>& arr, const QColor& c) {
            p.setPen(QPen(c, 1));
            QPointF prev; bool has = false;
            for (int k = 0; k < vis; k++) {
                int i = start + k;
                if (i >= (int)arr.size() || std::isnan(arr[i])) { has = false; continue; }
                QPointF pt(xOf(k), yK(arr[i]));
                if (has) p.drawLine(prev, pt);
                prev = pt; has = true;
            }
        };
        drawSubLine(k_, QColor(0xff, 0xff, 0xff));
        drawSubLine(d_, QColor(0xff, 0xd5, 0x4f));
        drawSubLine(j_, QColor(0xba, 0x68, 0xc8));
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(subR.left(), subR.top() - 3, qMax(260, (int)(260 * zs) + 40), labelH), Qt::AlignLeft,
                   QStringLiteral("KDJ(9,3,3)  K 白  D 黄  J 紫"));
    } else if (sub == 3) {
        // RSI: 范围 [0, 100]
        auto yR = [&](double v) { return subR.bottom() - v / 100.0 * subR.height(); };
        p.setPen(QPen(QColor(0x35, 0x3a, 0x45), 1, Qt::DotLine));
        for (double lv : {30.0, 50.0, 70.0}) {
            int y = (int)yR(lv);
            p.drawLine(subR.left(), y, subR.right(), y);
        }
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        for (double lv : {30.0, 50.0, 70.0}) {
            p.drawText(QRect(subR.right() + 4, (int)yR(lv) - labelH / 2, mR - 6, labelH),
                       Qt::AlignLeft | Qt::AlignVCenter, QString::number((int)lv));
        }
        auto drawSubLine = [&](const std::vector<double>& arr, const QColor& c) {
            p.setPen(QPen(c, 1));
            QPointF prev; bool has = false;
            for (int k = 0; k < vis; k++) {
                int i = start + k;
                if (i >= (int)arr.size() || std::isnan(arr[i])) { has = false; continue; }
                QPointF pt(xOf(k), yR(arr[i]));
                if (has) p.drawLine(prev, pt);
                prev = pt; has = true;
            }
        };
        drawSubLine(rsi6_,  QColor(0xff, 0xff, 0xff));
        drawSubLine(rsi12_, QColor(0xff, 0xd5, 0x4f));
        drawSubLine(rsi24_, QColor(0xba, 0x68, 0xc8));
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(subR.left(), subR.top() - 3, qMax(280, (int)(280 * zs) + 40), labelH), Qt::AlignLeft,
                   QStringLiteral("RSI(6,12,24)  RSI6 白  RSI12 黄  RSI24 紫"));
    } else if (sub == 4) {
        // WR(14): 0~100, >80 超卖 <20 超买
        auto yW = [&](double v) { return subR.bottom() - v / 100.0 * subR.height(); };
        p.setPen(QPen(QColor(0x35, 0x3a, 0x45), 1, Qt::DotLine));
        for (double lv : {20.0, 80.0}) {
            int y = (int)yW(lv);
            p.drawLine(subR.left(), y, subR.right(), y);
        }
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        for (double lv : {20.0, 80.0}) {
            p.drawText(QRect(subR.right() + 4, (int)yW(lv) - labelH / 2, mR - 6, labelH),
                       Qt::AlignLeft | Qt::AlignVCenter, QString::number((int)lv));
        }
        auto drawSubLine = [&](const std::vector<double>& arr, const QColor& c) {
            p.setPen(QPen(c, 1));
            QPointF prev; bool has = false;
            for (int k = 0; k < vis; k++) {
                int i = start + k;
                if (i >= (int)arr.size() || std::isnan(arr[i])) { has = false; continue; }
                QPointF pt(xOf(k), yW(arr[i]));
                if (has) p.drawLine(prev, pt);
                prev = pt; has = true;
            }
        };
        drawSubLine(wr14_, QColor(0xff, 0xff, 0xff));
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(subR.left(), subR.top() - 3, qMax(280, (int)(280 * zs) + 40), labelH), Qt::AlignLeft,
                   QStringLiteral("WR(14)  上80超卖 下20超买"));
    } else if (sub == 5) {
        // CCI(14): 参考线 ±100
        double vmax = 100.0;
        for (int i = start; i < n; i++)
            if (!std::isnan(cci14_[i])) vmax = std::max(vmax, std::fabs(cci14_[i]));
        vmax = std::ceil(vmax / 100.0) * 100.0;
        double y0 = subR.center().y();
        auto yC = [&](double v) { return y0 - v / vmax * (subR.height() / 2 - 2); };
        p.setPen(QPen(QColor(0x35, 0x3a, 0x45), 1, Qt::DotLine));
        p.drawLine(subR.left(), (int)y0, subR.right(), (int)y0);
        p.setPen(QPen(QColor(0x50, 0x45, 0x25), 1, Qt::DotLine));
        for (double lv : {100.0, -100.0}) {
            int y = (int)yC(lv);
            p.drawLine(subR.left(), y, subR.right(), y);
        }
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(subR.right() + 4, (int)y0 - labelH / 2, mR - 6, labelH),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("0"));
        p.drawText(QRect(subR.right() + 4, subR.top(), mR - 6, labelH),
                   Qt::AlignLeft, QString::number((int)vmax));
        p.drawText(QRect(subR.right() + 4, subR.bottom() - 14, mR - 6, labelH),
                   Qt::AlignLeft, QString::number((int)-vmax));
        auto drawSubLine = [&](const std::vector<double>& arr, const QColor& c) {
            p.setPen(QPen(c, 1));
            QPointF prev; bool has = false;
            for (int k = 0; k < vis; k++) {
                int i = start + k;
                if (i >= (int)arr.size() || std::isnan(arr[i])) { has = false; continue; }
                QPointF pt(xOf(k), yC(arr[i]));
                if (has) p.drawLine(prev, pt);
                prev = pt; has = true;
            }
        };
        drawSubLine(cci14_, QColor(0xff, 0xff, 0xff));
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(subR.left(), subR.top() - 3, qMax(300, (int)(300 * zs) + 40), labelH), Qt::AlignLeft,
                   QStringLiteral("CCI(14)  >+100超买  <-100超卖"));
    } else {
        // OBV 能量潮
        double lo2 = 1e18, hi2 = -1e18;
        for (int i = start; i < n; i++) {
            if (std::isnan(obv_[i])) continue;
            lo2 = std::min(lo2, obv_[i]);
            hi2 = std::max(hi2, obv_[i]);
        }
        if (hi2 <= lo2) { hi2 = lo2 + 1; }
        auto yO = [&](double v) {
            return subR.bottom() - (v - lo2) / (hi2 - lo2) * (subR.height() - 6) - 3;
        };
        if (lo2 < 0 && hi2 > 0) {
            p.setPen(QPen(QColor(0x40, 0x45, 0x50), 1, Qt::DotLine));
            int y = (int)yO(0);
            p.drawLine(subR.left(), y, subR.right(), y);
        }
        p.setPen(QPen(QColor(0xff, 0xff, 0xff), 1));
        QPointF prev; bool has = false;
        for (int k = 0; k < vis; k++) {
            int i = start + k;
            QPointF pt(xOf(k), yO(obv_[i]));
            if (has) p.drawLine(prev, pt);
            prev = pt; has = true;
        }
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        p.drawText(QRect(subR.left(), subR.top() - 3, qMax(300, (int)(300 * zs) + 40), labelH), Qt::AlignLeft,
                   QStringLiteral("OBV 能量潮 (涨日累加/跌日累减)"));
        p.drawText(QRect(subR.right() + 4, subR.top(), mR - 6, labelH),
                   Qt::AlignLeft, QString("%1万").arg(hi2 / 1e4, 0, 'f', 0));
        p.drawText(QRect(subR.right() + 4, subR.bottom() - 14, mR - 6, labelH),
                   Qt::AlignLeft, QString("%1万").arg(lo2 / 1e4, 0, 'f', 0));
    }

    // ---------- 日期刻度 ----------
    int step = std::max(1, vis / 6);
    for (int k = 0; k < vis; k += step) {
        int i = start + k;
        QString d = QString::fromStdString(bars_[i].date).mid(5);
        p.setPen(QColor(0x9a, 0xa0, 0xa8));
        int twi = qMax(60, (int)(60 * zs) + 16);
        p.drawText(QRect((int)xOf(k) - twi / 2, mainR.bottom() + 2, twi, labelH), Qt::AlignCenter, d);
    }

    // ---------- 标题 + 图例 ----------
    const Bar& last = bars_[n - 1];
    double prevC = n >= 2 ? bars_[n - 2].close : last.close;
    double pct = prevC > 0 ? (last.close - prevC) / prevC * 100 : 0;
    p.setFont(QFont(QString(), 10, QFont::Bold));
    p.setPen(QColor(0xe6, 0xe6, 0xe6));
    p.drawText(QRect(cr.left() + mL, cr.top() + 1, chartW - 240, 20), Qt::AlignLeft,
               QString("%1   收 %2  %3%   [近%4根, 滚轮缩放]")
                   .arg(title_).arg(last.close, 0, 'f', 2)
                   .arg(pct, 0, 'f', 2).arg(vis));
    // 图例(按勾选动态)
    p.setFont(QFont(QStringLiteral("Consolas"), 8));
    struct Leg { QCheckBox* cb; const char* name; QColor c; };
    Leg legs[] = {
        {cbMA5_, "MA5", QColor(0xff,0xd5,0x4f)},   {cbMA10_, "MA10", QColor(0x4f,0xc3,0xf7)},
        {cbMA20_, "MA20", QColor(0xba,0x68,0xc8)}, {cbMA60_, "MA60", QColor(0x81,0xc7,0x84)},
        {cbMA120_, "MA120", QColor(0xff,0x8a,0x50)}, {cbMA250_, "MA250", QColor(0xff,0x69,0xb4)},
        {cbBOLL_, "BOLL", QColor(0x64,0xb5,0xf6)},
    };
    int lx = cr.left() + mL + chartW - 7 * 50;
    p.setFont(QFont(QStringLiteral("Consolas"), 8));
    for (auto& lg : legs) {
        if (!lg.cb->isChecked()) continue;
        p.setPen(lg.c);
        p.drawText(lx, cr.top() + 14, lg.name);
        lx += 48;
    }

    // ---------- 悬停十字线 + 信息框 ----------
    if (hoverIdx_ >= 0 && hoverIdx_ < vis) {
        int i = start + hoverIdx_;
        int x = (int)xOf(hoverIdx_);
        p.setPen(QPen(QColor(0x80, 0x86, 0x90), 1, Qt::DashLine));
        p.drawLine(x, mainR.top(), x, subR.bottom());
        const Bar& b = bars_[i];
        double pc = (i > 0 && bars_[i - 1].close > 0)
                  ? (b.close - bars_[i - 1].close) / bars_[i - 1].close * 100 : 0;
        QString info = QString("%1  开%2 高%3 低%4 收%5  %6%  量%7万手")
            .arg(QString::fromStdString(b.date))
            .arg(b.open, 0, 'f', 2).arg(b.high, 0, 'f', 2).arg(b.low, 0, 'f', 2)
            .arg(b.close, 0, 'f', 2).arg(pc, 0, 'f', 2).arg(b.volume / 1e4, 0, 'f', 1);
        // 附当前副图指标值
        if (sub == 1 && i < (int)dif_.size())
            info += QString("   DIF %1 DEA %2 MACD %3")
                .arg(dif_[i], 0, 'f', 3).arg(dea_[i], 0, 'f', 3).arg(macd_[i], 0, 'f', 3);
        else if (sub == 2 && i < (int)k_.size() && !std::isnan(k_[i]))
            info += QString("   K %1 D %2 J %3")
                .arg(k_[i], 0, 'f', 1).arg(d_[i], 0, 'f', 1).arg(j_[i], 0, 'f', 1);
        else if (sub == 3 && i < (int)rsi6_.size() && !std::isnan(rsi6_[i]))
            info += QString("   RSI6 %1 RSI12 %2")
                .arg(rsi6_[i], 0, 'f', 1).arg(rsi12_[i], 0, 'f', 1);
        else if (sub == 4 && i < (int)wr14_.size() && !std::isnan(wr14_[i]))
            info += QString("   WR %1").arg(wr14_[i], 0, 'f', 1);
        else if (sub == 5 && i < (int)cci14_.size() && !std::isnan(cci14_[i]))
            info += QString("   CCI %1").arg(cci14_[i], 0, 'f', 1);
        else if (sub == 6 && i < (int)obv_.size())
            info += QString("   OBV %1万").arg(obv_[i] / 1e4, 0, 'f', 0);
        p.setFont(f);
        QFontMetrics fm(p.font());
        int tw = fm.horizontalAdvance(info) + 12;
        if (tw > mainR.width() - 12) tw = mainR.width() - 12;
        QRect box(mainR.left() + 6, mainR.top() + 6, tw, qMax(20, int(16 * zs) + 4));
        p.fillRect(box, QColor(0x20, 0x24, 0x2b, 235));
        p.setPen(QColor(0xe8, 0xe8, 0xe8));
        p.drawText(box.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, info);
    }
}

// ==================== 交互 ====================
void KLineChart::mouseMoveEvent(QMouseEvent* e) {
    int n = (int)bars_.size();
    if (n < 5) return;
    int vis = std::min(showDays_, n);
    int mL = 10, mR = 64;
    int chartW = canvasRect().width() - mL - mR;
    if (chartW <= 0) return;
    double binW = (double)chartW / vis;
    int k = (int)((e->pos().x() - (canvasRect().left() + mL)) / binW);
    if (k < 0) k = 0;
    if (k >= vis) k = vis - 1;
    if (k != hoverIdx_) { hoverIdx_ = k; update(); }
}

void KLineChart::leaveEvent(QEvent*) {
    hoverIdx_ = -1;
    update();
}

void KLineChart::wheelEvent(QWheelEvent* e) {
    int n = (int)bars_.size();
    if (e->angleDelta().y() > 0)
        showDays_ = std::max(30, (int)(showDays_ / 1.25));
    else
        showDays_ = std::min(std::min(500, n), (int)(showDays_ * 1.25));
    update();
}
