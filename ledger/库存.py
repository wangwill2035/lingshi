# -*- coding: utf-8 -*-
"""库存台账（行囊存货，与摊位日结分开记）。

规则：
  基数 = 当前行囊里的存货数；以后 入库 累加，上架 减少。
  简版口报：等级 + 4 个整数（戒、耳、镯、佩），0 表示没有，可多级连报。

用法：
  python 库存.py 基数 2026-11-01 10:17 "80:15,0,2,8,100:11,10,2,8"   # 重置为当前存货
  python 库存.py 入库 2026-11-01 14:00 "80:3,0,0,2"                 # 进货：+
  python 库存.py 上架 2026-11-01 15:00 "100:1,0,0,0"               # 摆上摊位：-
"""
import csv
import json
import os
import sys

BASE = r"C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰"
INV_DIR = os.path.join(BASE, "库存")
LEDGER = os.path.join(INV_DIR, "库存流水.csv")
CUR = os.path.join(INV_DIR, "库存.json")
PRICES = os.path.join(BASE, "data", "prices.json")

ORDER = ["ring", "ear", "brace", "pend"]
KIND_CN = {"ring": "戒指", "ear": "耳饰", "brace": "手镯", "pend": "佩饰"}
HEADER = ["日期", "时间", "类型", "款号", "类型名", "等级", "名称", "数量", "变动后库存"]


def load_prices():
    with open(PRICES, encoding="utf-8") as f:
        return {it["code"]: it for it in json.load(f)["items"]}


def parse_items(text, prices):
    rows, level, qtys = [], None, []

    def flush():
        nonlocal level, qtys
        if level is None:
            return
        if len(qtys) != 4:
            raise SystemExit(f"{level}级应报 4 个数（戒、耳、镯、佩），收到 {len(qtys)} 个")
        for kind, q in zip(ORDER, qtys):
            code = f"{kind}_{level}"
            info = prices.get(code)
            if not info:
                raise SystemExit(f"未知款号：{code}")
            rows.append([code, KIND_CN[kind], str(level), info["name"], int(q)])
        level = None
        qtys = []

    for part in [p.strip() for p in text.replace("，", ",").replace("：", ":").split(",") if p.strip()]:
        if ":" in part:
            flush()
            lv_s, q_s = part.split(":", 1)
            level = int(lv_s.replace("级", "").strip())
            if q_s.strip():
                qtys.append(int(q_s))
        elif level is not None:
            qtys.append(int(part))
        else:
            raise SystemExit(f"格式不对：{part}")
    flush()
    return rows


def load_cur():
    if os.path.exists(CUR):
        with open(CUR, encoding="utf-8") as f:
            return {k: int(v) for k, v in json.load(f).items()}
    return {}


def save_cur(cur, prices):
    os.makedirs(INV_DIR, exist_ok=True)
    out = {k: v for k, v in sorted(cur.items()) if v != 0}
    with open(CUR, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
    # 同步给悬浮窗面板读（与 stock.json 同目录）
    try:
        import datetime
        wdir = os.path.join(os.environ.get("APPDATA", ""), "MHXY-RateWidget")
        os.makedirs(wdir, exist_ok=True)
        items = []
        for code in sorted(out, key=lambda c: (int(c.split("_")[1]), ORDER.index(c.split("_")[0]))):
            info = prices.get(code, {})
            items.append({
                "code": code,
                "name": info.get("name", code),
                "qty": out[code],
                "unit_price_wan": info.get("price"),
            })
        snap = {
            "updated_at": datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
            "note": "行囊库存",
            "items": items,
        }
        with open(os.path.join(wdir, "storage.json"), "w", encoding="utf-8") as f:
            json.dump(snap, f, ensure_ascii=False, indent=2)
    except Exception as e:
        print(f"警告：storage.json 同步失败：{e}")


def main():
    if len(sys.argv) < 5:
        raise SystemExit(__doc__)
    op, date, hhmm, text = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    if op not in ("基数", "入库", "上架"):
        raise SystemExit("类型只能是 基数 / 入库 / 上架")
    prices = load_prices()
    rows = parse_items(text, prices)
    for r in rows:
        if r[4] < 0:
            raise SystemExit("数量不能是负数")
    cur = load_cur()

    out_rows = []
    for code, kind_cn, lv, name, qty in rows:
        if op == "基数":
            cur[code] = qty
        elif op == "入库":
            cur[code] = cur.get(code, 0) + qty
        else:  # 上架
            have = cur.get(code, 0)
            if have < qty:
                raise SystemExit(f"{name} 库存只有 {have} 件，上架 {qty} 件超了")
            cur[code] = have - qty
        out_rows.append([date, hhmm, op, code, kind_cn, lv, name, str(qty), str(cur[code])])

    save_cur(cur, prices)
    os.makedirs(INV_DIR, exist_ok=True)
    new = not os.path.exists(LEDGER)
    with open(LEDGER, "a", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        if new:
            w.writerow(HEADER)
        w.writerows(out_rows)

    print(f"已记库存{op} {date} {hhmm}：" + "，".join(f"{r[6]}×{r[7]}" for r in out_rows))
    # 上架 = 从库存拿出摆上摊位：库存已减，同步给摊位在售记一笔补货（含日结重算）
    if op == "上架":
        import subprocess
        restock = os.path.join(BASE, "每日盘点", "记一笔.py")
        print("同步摊位补货…")
        subprocess.run([sys.executable, restock, "补货", date, hhmm, text], check=False)
    print("当前库存（>0）：")
    for code in sorted(cur, key=lambda c: (int(c.split("_")[1]), ORDER.index(c.split("_")[0]))):
        if cur[code]:
            print(f"  {code} {prices.get(code,{}).get('name','')} = {cur[code]}")


if __name__ == "__main__":
    main()
