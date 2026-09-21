#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
合成 A 股市场生成器(用于"涨停后 N 天"日历效应研究)。

特性(尽力贴近真市):
  - 主板 +/-10% 涨跌停; 用 LIMIT 参数可配创业板/科创板 +/-20%。
  - 涨停稀少且成群: 每日以概率 p_up 触发涨停, 题材接力(p_cont)让次日继续涨停(连板)。
  - 一字板(open==limit, 封死, 买不进) vs 换手涨停(有量, 可追)。
  - 跌停对称。 正常日收益 ~ Normal(mu, sigma)。

用法:
  python3 gen_market.py [输出csv] [股票数] [天数] [seed] [p_cont] [limit] [p_up] [p_down]
默认输出: tools/data/mkt_limitup.csv
"""
import csv, random, sys, os, datetime

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tools/data/mkt_limitup.csv"
    N_STOCKS = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    N_DAYS   = int(sys.argv[3]) if len(sys.argv) > 3 else 600
    SEED     = int(sys.argv[4]) if len(sys.argv) > 4 else 20240607
    P_CONT   = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0
    LIMIT    = float(sys.argv[6]) if len(sys.argv) > 6 else 0.10
    P_UP     = float(sys.argv[7]) if len(sys.argv) > 7 else 0.015
    P_DOWN   = float(sys.argv[8]) if len(sys.argv) > 8 else 0.010
    rng = random.Random(SEED)
    mu, sigma = 0.0003, 0.014
    all_rows = []
    total_ul = total_dl = total_normal = 0
    for s in range(N_STOCKS):
        code = "%06d" % (600000 + s)
        p = round(rng.uniform(8.0, 60.0), 2)
        momentum = 0; last_ul = False
        for t in range(N_DAYS):
            p_up = P_UP + P_CONT * 0.10 * momentum
            p_up = min(p_up, 0.45)
            is_ul = is_dl = False
            if rng.random() < p_up:
                is_ul = True; momentum = 1 + int(rng.random()*3)
            elif rng.random() < P_DOWN:
                is_dl = True; momentum = 0
            else:
                momentum = momentum - 1
                if momentum < 0: momentum = 0
            prev = p
            if is_ul:
                cl = round(prev * (1 + LIMIT), 2)
                yizi = (last_ul and rng.random() < 0.7) or rng.random() < 0.35
                if yizi:
                    op = cl; vol = rng.uniform(500, 4000); hi = cl; lo = cl; total_ul += 1
                else:
                    op = round(cl * (1 - rng.uniform(0.001, 0.012)), 2)
                    hi = cl; lo = min(op, cl)
                    vol = rng.uniform(80000, 600000); total_ul += 1
            elif is_dl:
                cl = round(prev * (1 - LIMIT), 2)
                yizi = rng.random() < 0.3
                if yizi:
                    op = cl; vol = rng.uniform(500, 4000); hi = cl; lo = cl; total_dl += 1
                else:
                    op = round(cl * (1 + rng.uniform(0.001, 0.012)), 2)
                    hi = max(op, cl); lo = cl; vol = rng.uniform(80000, 600000); total_dl += 1
            else:
                r = rng.gauss(mu, sigma); cl = round(prev * (1 + r), 2)
                op = round(prev * (1 + rng.gauss(0, 0.004)), 2)
                hi = round(max(op, cl) * (1 + rng.uniform(0.0, 0.006)), 2)
                lo = round(min(op, cl) * (1 - rng.uniform(0.0, 0.006)), 2)
                vol = int(rng.lognormvariate(12.0, 0.5)); total_normal += 1
            all_rows.append((code, cl, op, hi, lo, vol, is_ul, is_dl))
            p = cl; last_ul = is_ul
    d = datetime.date(2021, 1, 4); dates = []
    while len(dates) < N_DAYS:
        if d.weekday() < 5: dates.append(d.strftime("%Y-%m-%d"))
        d += datetime.timedelta(days=1)
    by_day = {dt: [] for dt in dates}
    for i, (code, cl, op, hi, lo, vol, is_ul, is_dl) in enumerate(all_rows):
        by_day[dates[i % N_DAYS]].append((code, op, hi, lo, cl, vol, is_ul, is_dl))
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["code", "date", "open", "high", "low", "close", "volume", "is_limitup", "is_limitdown"])
        for dt in dates:
            for tup in by_day[dt]:
                (code, op, hi, lo, cl, vol, is_ul, is_dl) = tup
                w.writerow([code, dt, format(op, ".2f"), format(hi, ".2f"),
                            format(lo, ".2f"), format(cl, ".2f"), str(int(vol)),
                            1 if is_ul else 0, 1 if is_dl else 0])
    N = len(dates) * N_STOCKS
    print("生成 %s  OK" % os.path.basename(out))
    print("  股票数=%d 交易日=%d 总样本=%d" % (N_STOCKS, N_DAYS, N))
    print("  涨停=%d  跌停=%d  正常=%d  (涨停占比 %.2f%%)" %
          (total_ul, total_dl, total_normal, 100.0 * total_ul / max(N, 1)))
    print("  p_cont(涨停后接力)=%s" % P_CONT)

if __name__ == "__main__":
    main()
