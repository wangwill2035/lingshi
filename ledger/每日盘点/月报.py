"""月度销售统计 —— 读整月每天的 `YYYY-MM-DD.csv`，算出各款销量、销售额、好卖排名

用法：
    python 月报.py [YYYY-MM] [--dir 目录]

不传月份则取目录里已有的最新月份。默认目录是「每日盘点」。
产出：`月报-YYYY-MM.csv`（总览区 + 按款排名区 + 按日明细区），同时打印一段文字摘要。

口径：
- 每天销量按款 = 该日首次盘点 + 该日补货 − 该日末次盘点（与 日结.py 完全一致，直接复用其 compute）；
- 只有「已日结」（当天有两次盘点）的日子才计入销量，未日结的日子列出但不计入合计；
- 销售额按款的执行单价（data\\prices.json）逐日计算，再按当天的汇率折人民币；
- 中途「核对」行只用于即时对账，不进月报销量。
"""

import csv
import datetime
import json
import os
import sys

BASE = r"C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰"
DAILY_DIR = os.path.join(BASE, "每日盘点")
PRICES = os.path.join(BASE, "data", "prices.json")
STATE = os.path.join(os.environ["APPDATA"], "MHXY-RateWidget", "state.json")

sys.path.insert(0, DAILY_DIR)
import 日结 as rj  # noqa: E402 复用日结的计算口径，避免算法漂移


def load_prices():
    with open(PRICES, encoding="utf-8") as f:
        data = json.load(f)
    return {it["code"]: it for it in data["items"]}


def current_rate():
    try:
        with open(STATE, encoding="utf-8-sig") as f:
            return float(json.load(f).get("price") or 224.01)
    except Exception:
        return 224.01


def day_rate(date, ddir, fallback):
    """优先取当天表汇总区里的汇率（当天真实成交口径），取不到用当前汇率。"""
    path = os.path.join(ddir, f"{date}.csv")
    if not os.path.exists(path):
        return fallback
    with open(path, encoding="utf-8-sig") as f:
        rows = list(csv.reader(f))
    for r in rows:
        if len(r) >= 2 and r[0].strip() == "汇率(元/3000万)":
            try:
                return float(r[1])
            except ValueError:
                return fallback
    return fallback


def read_body(path):
    with open(path, encoding="utf-8-sig") as f:
        rows = list(csv.reader(f))
    body = []
    for r in rows[1:]:
        if not r or not "".join(r).strip():
            break
        if r[0].strip() == "汇总":
            break
        if len(r) < 8:
            continue
        body.append(r)
    return body


def month_files(month, ddir):
    out = []
    if not os.path.isdir(ddir):
        return out
    for name in sorted(os.listdir(ddir)):
        if not name.endswith(".csv") or len(name) != 14:
            continue
        if name[:7] == month and name[:4].isdigit():
            out.append(name[:-4])
    return out


def scan(month, ddir, prices, fallback):
    days = []
    for date in month_files(month, ddir):
        body = read_body(os.path.join(ddir, f"{date}.csv"))
        if not body:
            continue
        days.append({
            "date": date,
            "res": rj.compute(date, body, prices),
            "rate": day_rate(date, ddir, fallback),
        })
    return days


def build(month, days, prices):
    settled = [d for d in days if d["res"]["settled"]]
    items = {}
    for d in settled:
        rate_wan = d["rate"] / 3000.0
        for code, q in d["res"]["detail"].items():
            if not q or q <= 0:
                continue
            p = prices.get(code, {})
            rec = items.setdefault(code, {
                "code": code, "name": p.get("name", code), "type": p.get("type", ""),
                "level": p.get("level", 0), "price": p.get("price"),
                "qty": 0, "wan": 0.0, "rmb": 0.0, "days": {},
            })
            rec["qty"] += q
            rec["days"][d["date"]] = rec["days"].get(d["date"], 0) + q
            if p.get("price") is not None:
                rec["wan"] += q * p["price"]
                rec["rmb"] += q * p["price"] * rate_wan

    total_qty = sum(r["qty"] for r in items.values())
    total_wan = sum(r["wan"] for r in items.values())
    total_rmb = sum(r["rmb"] for r in items.values())
    return {
        "month": month,
        "days": days,
        "days_settled": len(settled),
        "total_qty": total_qty,
        "total_wan": total_wan,
        "total_rmb": total_rmb,
        "restock_cnt": sum(d["res"]["restock_cnt"] for d in days),
        "restock_qty": sum(d["res"]["restock_qty"] for d in days),
        "items": sorted(items.values(), key=lambda r: (-r["wan"], -r["qty"], r["name"])),
        "unsold": [p for c, p in prices.items() if p.get("price") is not None and c not in items],
    }


def write_report(rep, ddir):
    path = os.path.join(ddir, f"月报-{rep['month']}.csv")
    settled = [d for d in rep["days"] if d["res"]["settled"]]
    out = [["月度经营汇总"], ["项目", "值"]]
    out.append(["月份", rep["month"]])
    out.append(["统计口径", "每日销量=首次盘点+补货−末次盘点；只计灵饰；只计已日结的日子"])
    if settled:
        out.append(["起算日", f"{settled[0]['date']}（期初 {settled[0]['res']['start_qty']} 件）"])
        out.append(["截止日", f"{settled[-1]['date']}（期末 {settled[-1]['res']['end_qty']} 件）"])
    out.append(["已日结天数", str(rep["days_settled"])])
    out.append(["未日结天数", str(len(rep["days"]) - rep["days_settled"])])
    out.append(["补货次数", str(rep["restock_cnt"])])
    out.append(["补货件数", str(rep["restock_qty"])])
    out.append(["月销量(件)", str(rep["total_qty"])])
    out.append(["月销售额(万)", f"{rep['total_wan']:.0f}"])
    out.append(["折人民币(元)", f"{rep['total_rmb']:.2f}"])
    out.append(["有动销款数", str(len(rep["items"]))])
    out.append(["未动销在售款数", str(len(rep["unsold"]))])
    if rep["days_settled"]:
        out.append(["日均销量(件)", f"{rep['total_qty'] / rep['days_settled']:.1f}"])
        out.append(["日均销售额(万)", f"{rep['total_wan'] / rep['days_settled']:.0f}"])
    out.append([])

    out.append(["按款排名（销售额降序）"])
    out.append(["名次", "款号", "名称", "类别", "等级", "执行单价(万)", "销量(件)",
                "销售额(万)", "折人民币(元)", "动销天数", "动销日均(件)", "占销售额"])
    for i, r in enumerate(rep["items"], 1):
        days_on = len(r["days"])
        share = (r["wan"] / rep["total_wan"] * 100) if rep["total_wan"] else 0
        out.append([
            str(i), r["code"], r["name"], r["type"], str(r["level"]),
            "" if r["price"] is None else f"{r['price']:.0f}",
            str(r["qty"]), f"{r['wan']:.0f}", f"{r['rmb']:.2f}",
            str(days_on), f"{(r['qty'] / days_on if days_on else 0):.2f}", f"{share:.1f}%",
        ])
    out.append([])

    out.append(["按日明细"])
    out.append(["日期", "已日结", "期初(件)", "补货(次)", "补货(件)", "期末(件)",
                "销量(件)", "销售额(万)", "折人民币(元)", "汇率"])
    for d in rep["days"]:
        res = d["res"]
        settled_ok = "是" if res["settled"] else "否"
        wan = "" if res["sale_wan"] is None else f"{res['sale_wan']:.0f}"
        rmb = "" if res["sale_wan"] is None else f"{res['sale_wan'] * d['rate'] / 3000.0:.2f}"
        out.append([
            d["date"], settled_ok, str(res["start_qty"]), str(res["restock_cnt"]),
            str(res["restock_qty"]),
            "" if res["end_qty"] is None else str(res["end_qty"]),
            "" if res["sale_qty"] is None else str(res["sale_qty"]),
            wan, rmb, f"{d['rate']:.2f}",
        ])
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        csv.writer(f).writerows(out)
    return path


def main():
    args = [a for a in sys.argv[1:]]
    ddir = DAILY_DIR
    if "--dir" in args:
        i = args.index("--dir")
        ddir = args[i + 1]
        del args[i:i + 2]
    if args and args[0]:
        month = args[0]
    else:
        files = [f[:-4] for f in os.listdir(ddir) if f.endswith(".csv") and len(f) == 14 and f[:4].isdigit()]
        month = max(files)[:7] if files else datetime.date.today().strftime("%Y-%m")

    prices = load_prices()
    days = scan(month, ddir, prices, current_rate())
    if not days:
        print(f"{month}: 目录里没有该月的日表（{ddir}）")
        return
    rep = build(month, days, prices)
    path = write_report(rep, ddir)

    lines = [
        f"== {month} 月报 ==",
        f"已日结 {rep['days_settled']} 天 / 未日结 {len(rep['days']) - rep['days_settled']} 天"
        f" · 补货 {rep['restock_cnt']} 次 {rep['restock_qty']} 件",
        f"月销量 {rep['total_qty']} 件 · {rep['total_wan']:.0f} 万 ≈ ¥{rep['total_rmb']:.2f}",
    ]
    if rep["days_settled"]:
        lines.append(f"日均 {rep['total_qty'] / rep['days_settled']:.1f} 件 · {rep['total_wan'] / rep['days_settled']:.0f} 万")
    lines.append("好卖款（按销售额）：")
    for i, r in enumerate(rep["items"][:8], 1):
        share = (r["wan"] / rep["total_wan"] * 100) if rep["total_wan"] else 0
        lines.append(f"  {i}. {r['name']}({r['level']}) {r['qty']} 件 {r['wan']:.0f} 万 ≈ ¥{r['rmb']:.0f} · {share:.1f}%")
    if not rep["items"]:
        lines.append("  （本月还没有已日结的日子，暂无销量）")
    lines.append(f"写入 {os.path.basename(path)}")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
