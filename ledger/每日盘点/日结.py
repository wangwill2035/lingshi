"""每日盘点 / 补货 / 日结计算  —— 每天一张表

用法： python 日结.py [YYYY-MM-DD]
不传日期则取「每日盘点」目录里最新的一天。

约定（用户口径）：
- 每天 0 点后的截图 = 前一天的盘点（当天的期初结存，也是「在售基数」）；
- 白天 7 点起及之后的补货截图 = 增量，按张累加，记时间 / 款 / 数量；
- 次日 0 点盘点 = 期末结存；
- 当日销量 = 期初 + 补货 − 期末（按款）；
- 销售额 = Σ(销量 × 当日执行单价)，单位万梦幻币，再按当日汇率折人民币；
- 每天只出一个文件：`每日盘点\\YYYY-MM-DD.csv`，上半是明细表，下半是同一张表里的汇总区。
"""

import csv
import datetime
import json
import os
import sys

BASE = r"C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰"
DAILY_DIR = os.path.join(BASE, "每日盘点")
PRICES = os.path.join(BASE, "data", "prices.json")
WIDGET_DIR = os.path.join(os.environ["APPDATA"], "MHXY-RateWidget")
STATE = os.path.join(WIDGET_DIR, "state.json")
STOCK = os.path.join(WIDGET_DIR, "stock.json")
TODAY_JSON = os.path.join(WIDGET_DIR, "today.json")
YEST_JSON = os.path.join(WIDGET_DIR, "yesterday.json")

HEADER = ["日期", "时间", "类型", "款号", "类型名", "等级", "名称", "数量", "执行单价(万)", "金额(万)", "备注"]
SUM_MARK = "汇总"
SUM_HEADER = ["项目", "值"]


def load_unit_prices():
    with open(PRICES, encoding="utf-8") as f:
        data = json.load(f)
    return {it["code"]: it for it in data["items"]}


def load_rate():
    try:
        with open(STATE, encoding="utf-8-sig") as f:
            st = json.load(f)
        return float(st.get("price") or 224.01)
    except Exception:
        return 224.01


def read_daily(date):
    """读取当天表的明细区（明细区到空行或「汇总」标记为止）。"""
    path = os.path.join(DAILY_DIR, f"{date}.csv")
    if not os.path.exists(path):
        return path, []
    with open(path, encoding="utf-8-sig") as f:
        rows = list(csv.reader(f))
    body = []
    for r in rows[1:]:
        if not r or not "".join(r).strip():
            break
        if r[0].strip() == SUM_MARK:
            break
        if len(r) < 8:
            continue
        body.append(r)
    return path, body


def compute(date, body, prices):
    inv = [r for r in body if r[2] == "盘点"]
    restocks = [r for r in body if r[2] == "补货"]
    stamps = []
    for r in inv:
        key = (r[0], r[1])
        if not stamps or stamps[-1] != key:
            stamps.append(key)
    first = stamps[0] if len(stamps) >= 1 else None
    last = stamps[-1] if len(stamps) >= 2 else None

    def qty_map(rows):
        m = {}
        for r in rows:
            m[r[3]] = m.get(r[3], 0) + int(float(r[7]))
        return m

    start = qty_map([r for r in inv if (r[0], r[1]) == first]) if first else {}
    end = qty_map([r for r in inv if (r[0], r[1]) == last]) if last else {}
    add = qty_map(restocks)

    codes = sorted(set(list(start) + list(add) + list(end)))
    sale = {}
    for c in codes:
        if last is None:
            sale[c] = None
        else:
            sale[c] = start.get(c, 0) + add.get(c, 0) - end.get(c, 0)

    wan = 0.0
    unknown = []
    if last is not None:
        for c, q in sale.items():
            price = prices.get(c, {}).get("price")
            if q and q > 0:
                if price is None:
                    unknown.append(c)
                else:
                    wan += q * price

    # 中途核对：只跟「盘点基数 + 补货」比，得出即时销量，不改任何在售数据
    def key_of(r):
        return f"{r[0]} {r[1]}"

    prov = None
    checks = [r for r in body if r[2] == "核对"]
    if checks:
        ckeys = []
        for r in checks:
            k = key_of(r)
            if not ckeys or ckeys[-1] != k:
                ckeys.append(k)
        last_ck = ckeys[-1]
        base_k = None
        for k in stamps:
            if f"{k[0]} {k[1]}" <= last_ck:
                base_k = f"{k[0]} {k[1]}"
        base_rows = [r for r in inv if key_of(r) == base_k] if base_k else []
        mid_add = [r for r in restocks if (base_k or "") < key_of(r) <= last_ck]
        base_q = qty_map(base_rows)
        add_q = qty_map(mid_add)
        ck_q = qty_map([r for r in checks if key_of(r) == last_ck])
        codes_ck = sorted(set(list(base_q) + list(add_q) + list(ck_q)))
        pv_sale = {}
        for c in codes_ck:
            pv_sale[c] = base_q.get(c, 0) + add_q.get(c, 0) - ck_q.get(c, 0)
        pv_wan = 0.0
        pv_unknown = []
        for c, q in pv_sale.items():
            if q and q > 0:
                price = prices.get(c, {}).get("price")
                if price is None:
                    pv_unknown.append(c)
                else:
                    pv_wan += q * price
        hhmm = checks[-1][1]
        prov = {
            "time": hhmm,
            "checked_at": last_ck,
            "base_qty": sum(base_q.values()),
            "restock_qty": sum(add_q.values()),
            "now_qty": sum(ck_q.values()),
            "sale_qty": sum(v for v in pv_sale.values() if v),
            "sale_wan": pv_wan,
            "unknown": pv_unknown,
            "detail": pv_sale,
        }

    return {
        "date": date,
        "settled": last is not None,
        "start_qty": sum(start.values()),
        "end_qty": sum(end.values()) if last is not None else None,
        "restock_cnt": len({(r[0], r[1]) for r in restocks}),
        "restock_qty": sum(add.values()),
        "sale_qty": sum(v for v in sale.values() if v) if last is not None else None,
        "sale_wan": wan if last is not None else None,
        "unknown": unknown,
        "detail": sale,
        "provisional": prov,
    }


def write_daily(path, detail_rows, res, rate):
    """把汇总区写回同一张表（明细区保持原样）。"""
    rate_wan = rate / 3000.0
    note_parts = []
    if res["unknown"]:
        note_parts.append("未报价款：" + "、".join(res["unknown"]))
    if res["settled"] and res["sale_qty"] is not None:
        neg = [c for c, q in res["detail"].items() if q is not None and q < 0]
        if neg:
            note_parts.append("销量为负待确认：" + "、".join(neg))
    if not res["settled"]:
        note_parts.append("期末盘点未到，销量待次日 0 点盘点后生成")

    sum_rows = [
        [SUM_MARK],
        SUM_HEADER,
        ["日期", res["date"]],
        ["状态", "已日结" if res["settled"] else "待次日盘点"],
        ["期初件数", str(res["start_qty"])],
        ["补货次数", str(res["restock_cnt"])],
        ["补货件数", str(res["restock_qty"])],
        ["期末件数", "" if res["end_qty"] is None else str(res["end_qty"])],
        ["销量件数", "" if res["sale_qty"] is None else str(res["sale_qty"])],
        ["销售额(万)", "" if res["sale_wan"] is None else f"{res['sale_wan']:.0f}"],
        ["折人民币(元)", "" if res["sale_wan"] is None else f"{res['sale_wan'] * rate_wan:.2f}"],
    ]
    if res.get("provisional"):
        pv = res["provisional"]
        sum_rows.extend([
            ["即时核对时间", pv["time"]],
            ["即时库存件数", str(pv["now_qty"])],
            ["即时销量件数", str(pv["sale_qty"])],
            ["即时销售额(万)", f"{pv['sale_wan']:.0f}"],
            ["即时折人民币(元)", f"{pv['sale_wan'] * rate_wan:.2f}"],
        ])
        if pv["unknown"]:
            note_parts.append("即时未报价款：" + "、".join(pv["unknown"]))
    sum_rows.append(["汇率(元/3000万)", f"{rate:.2f}"])
    sum_rows.append(["备注", "；".join(note_parts)])
    out = [HEADER] + [list(r) for r in detail_rows] + [[]] + sum_rows
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        csv.writer(f).writerows(out)


def write_today_json(res, rate):
    rate_wan = rate / 3000.0
    pv = res.get("provisional")
    payload = {
        "date": res["date"],
        "settled": res["settled"],
        "restock_cnt": res["restock_cnt"],
        "restock_qty": res["restock_qty"],
        "start_qty": res["start_qty"],
        "end_qty": res["end_qty"],
        "sale_qty": res["sale_qty"],
        "sale_wan": None if res["sale_wan"] is None else round(res["sale_wan"], 1),
        "rmb": None if res["sale_wan"] is None else round(res["sale_wan"] * rate_wan, 1),
        "provisional": None if not pv else {
            "time": pv["time"],
            "checked_at": pv["checked_at"],
            "base_qty": pv["base_qty"],
            "restock_qty": pv["restock_qty"],
            "now_qty": pv["now_qty"],
            "sale_qty": pv["sale_qty"],
            "sale_wan": round(pv["sale_wan"], 1),
            "rmb": round(pv["sale_wan"] * rate_wan, 1),
        },
        "rate": rate,
        "updated_at": datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
    }
    with open(TODAY_JSON, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
    return payload


def sync_stock(date, body, prices):
    """面板「在售快照」= 最新一次盘点基数 + 其后各次补货累加。"""
    if not body:
        return
    groups = []
    for r in body:
        key = (r[2], r[0], r[1])
        if groups and groups[-1]["key"] == key:
            groups[-1]["rows"].append(r)
        else:
            groups.append({"key": key, "rows": [r]})

    items = {}
    stamp = None
    restock_cnt = 0
    restock_qty = 0
    for grp in groups:
        kind, day, ts = grp["key"]
        if kind == "盘点":
            items = {}
            for r in grp["rows"]:
                items[r[3]] = items.get(r[3], 0) + int(float(r[7]))
            stamp = f"{day}T{ts}:00"
        elif kind == "补货":
            restock_cnt += 1
            for r in grp["rows"]:
                q = int(float(r[7]))
                items[r[3]] = items.get(r[3], 0) + q
                restock_qty += q
            stamp = f"{day}T{ts}:00"
        # 「核对」行只用于即时对账，绝不影响在售快照

    out = {"updated_at": stamp, "note": f"盘点基数 + 补货 {restock_cnt} 次 {restock_qty} 件", "items": []}
    for code, q in items.items():
        if q <= 0:
            continue
        p = prices.get(code, {})
        out["items"].append({
            "code": code,
            "name": p.get("name", code),
            "qty": q,
            "unit_price_wan": p.get("price"),
        })
    with open(STOCK, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)


def write_yesterday_json(date, prices, rate):
    """把前一天的日结另存一份，供面板「昨日」按钮直接读。"""
    prev = (datetime.datetime.strptime(date, "%Y-%m-%d").date() - datetime.timedelta(days=1)).isoformat()
    path, body = read_daily(prev)
    payload = {"date": prev, "settled": False, "start_qty": None, "restock_cnt": 0,
               "sale_qty": None, "sale_wan": None, "rmb": None, "rate": rate}
    if body:
        res = compute(prev, body, prices)
        payload.update({
            "settled": bool(res["settled"]),
            "start_qty": res["start_qty"],
            "restock_cnt": res["restock_cnt"],
            "end_qty": res["end_qty"],
            "sale_qty": res["sale_qty"],
            "sale_wan": None if res["sale_wan"] is None else round(res["sale_wan"], 1),
            "rmb": None if res["sale_wan"] is None else round(res["sale_wan"] * rate / 3000.0, 1),
            "detail": {c: q for c, q in res["detail"].items() if q},
        })
    with open(YEST_JSON, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
    return payload


def latest_date():
    files = [f for f in os.listdir(DAILY_DIR) if f.endswith(".csv") and f[:4].isdigit()]
    return max(f[:-4] for f in files) if files else datetime.date.today().isoformat()


def main():
    date = sys.argv[1] if len(sys.argv) > 1 else latest_date()
    prices = load_unit_prices()
    rate = load_rate()
    path, body = read_daily(date)
    if not body:
        print(f"{date}: 当天表里还没有盘点/补货记录（{path}）")
        return
    res = compute(date, body, prices)
    write_daily(path, body, res, rate)
    sync_stock(date, body, prices)
    payload = write_today_json(res, rate)
    write_yesterday_json(date, prices, rate)
    lines = [
        f"== {date} 日结 ==",
        f"期初 {res['start_qty']} 件 / 补货 {res['restock_cnt']} 次 {res['restock_qty']} 件 / 期末 "
        + (str(res["end_qty"]) if res["end_qty"] is not None else "待盘点"),
    ]
    if res["settled"]:
        lines.append(f"销量 {res['sale_qty']} 件，销售额 {res['sale_wan']:.0f} 万 ≈ ¥{res['sale_wan'] * rate / 3000:.1f}")
        for c, q in sorted(res["detail"].items()):
            if q:
                nm = prices.get(c, {}).get("name", c)
                lines.append(f"  {nm:<6} {q:>3} 件")
        if res["unknown"]:
            lines.append("  未报价款： " + ",".join(res["unknown"]))
    else:
        lines.append("期末盘点未到，销量待次日 0 点生成")
    pv = res.get("provisional")
    if pv:
        lines.append(
            f"即时核对 {pv['time']}：基数 {pv['base_qty']} + 补货 {pv['restock_qty']} → 在售 {pv['now_qty']} 件，"
            f"已销 {pv['sale_qty']} 件，{pv['sale_wan']:.0f} 万 ≈ ¥{pv['sale_wan'] * rate / 3000:.1f}（不影响在售数据）"
        )
    lines.append(f"写入 {os.path.basename(path)}（明细 + 汇总同表）")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
