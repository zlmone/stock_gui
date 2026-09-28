// klinechart.h — K线图控件 v2 (QPainter 自绘, 无外部依赖)
// 主图叠加: MA5/10/20/60/120/250 + BOLL(20,2)  —— 复选框控制
// 副图切换: 成交量 / MACD / KDJ / RSI / WR(14) / CCI(14) / OBV
// 交互: 悬停十字光标(含指标值) + 滚轮缩放
#pragma once
#include <QWidget>
#include <vector>
#include "engine.h"

class QCheckBox;
class QComboBox;
class QLabel;

class KLineChart : public QWidget {
    Q_OBJECT
public:
    explicit KLineChart(QWidget* parent = nullptr);
    void setData(const std::vector<Bar>& bars, const QString& title);
    void setShowDays(int days) { showDays_ = days; update(); }
    void setSubIndex(int idx);       // 副图切换: 0=VOL 1=MACD 2=KDJ 3=RSI 4=WR 5=CCI 6=OBV
    void setBollOn(bool on);         // BOLL 叠加开关
    void setPeriod(int p);           // 周期: 0=日K 1=周K 2=月K
    void setChipsOn(bool on);        // 筹码分布图开关
    void zoomIn();         // 放大: 显示更少K线(文字同步变大)
    void zoomOut();        // 缩小: 显示更多K线

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void computeAll();
    QRect canvasRect() const;

    std::vector<Bar> rawBars_;       // 原始日线
    std::vector<Bar> bars_;          // 当前周期K线
    QString title_;
    // 主图指标序列
    std::vector<double> ma5_, ma10_, ma20_, ma60_, ma120_, ma250_;
    std::vector<double> bollUp_, bollMid_, bollLow_;
    // 副图指标序列
    std::vector<double> dif_, dea_, macd_;
    std::vector<double> k_, d_, j_;
    std::vector<double> rsi6_, rsi12_, rsi24_;
    std::vector<double> wr14_, cci14_, obv_;
    // 筹码分布 (图形): 价格分桶的筹码量
    void computeChips();
    std::vector<double> chips_;
    double chipLo_ = 0, chipHi_ = 0, chipProfit_ = -1;
    double chipConc70_ = -1, chipConc90_ = -1;
    int period_ = 0;                 // 0=日 1=周 2=月
    QLabel* zoomLabel_ = nullptr;    // 缩放倍数读条
    double zoomScale() const;      // 当前缩放比例 (>=1)
    int showDays_ = 120;
    int hoverIdx_ = -1;

    // 控制栏
    QWidget* ctrlBar_ = nullptr;
    QCheckBox *cbMA5_ = nullptr, *cbMA10_ = nullptr, *cbMA20_ = nullptr;
    QCheckBox *cbMA60_ = nullptr, *cbMA120_ = nullptr, *cbMA250_ = nullptr, *cbBOLL_ = nullptr;
    QCheckBox *cbChips_ = nullptr;
    QComboBox* subCombo_ = nullptr;
    QComboBox* periodCombo_ = nullptr;
};
