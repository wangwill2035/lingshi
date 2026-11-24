# -*- coding: utf-8 -*-
"""按用户给的表格样式，把当前在售（stock.json）期初盘点做成 Excel。
用法：python 期初表.py [YYYY-MM-DD]   缺省用今天。
"""
import json, os, sys, datetime

BASE = r"C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰"
WIDGET = os.path.join(os.environ["APPDATA"], "MHXY-RateWidget")
DAILY = os.path.join(BASE, "每日盘点")

KIND_CN = {"ring": "戒指", "ear": "耳饰", "brace": "手镯", "pend": "佩饰"}
ORDER = ["ring", "ear", "brace", "pend"]

date = sys.argv[1] if len(sys.argv) > 1 else datetime.date.today().isoformat()

with open(os.path.join(WIDGET, "stock.json"), encoding="utf-8") as f:
    stock = json.load(f)
with open(os.path.join(WIDGET, "state.json"), encoding="utf-8") as f:
    price = float(json.load(f).get("price") or 224.01)

rows = []
for it in stock["items"]:
    code = it["code"]
    kind, lv = code.split("_")
    if not it["qty"]:
        continue
    rows.append({"level": int(lv), "kind": kind, "name": it["name"],
                 "price": it["unit_price_wan"], "qty": it["qty"]})
rows.sort(key=lambda r: (r["level"], ORDER.index(r["kind"])))

rate = price / 3000.0
for r in rows:
    r["gold"] = r["price"] * r["qty"]
    r["rmb"] = r["gold"] * rate

total_qty = sum(r["qty"] for r in rows)
total_gold = sum(r["gold"] for r in rows)
total_rmb = sum(r["rmb"] for r in rows)
kinds = len(rows)

from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment, Border, Side

wb = Workbook()
ws = wb.active
ws.title = f"{date[5:]}期初"

headers = ["类别", "类型", "等级", "名称", "执行单价", "件数", "金币小计", "折人民币(元)"]
ws.append(headers)

for r in rows:
    ws.append(["灵饰", KIND_CN[r["kind"]], r["level"], r["name"],
               r["price"], r["qty"], r["gold"], round(r["rmb"], 2)])

last = ws.max_row + 1
ws.append(["", "", "", f"灵饰 {kinds} 款", "", total_qty, total_gold, round(total_rmb, 2)])

# 样式：表头绿底加粗居中；细边框；列宽
green = PatternFill("solid", fgColor="A9D08E")
thin = Side(style="thin", color="B0B0B0")
border = Border(left=thin, right=thin, top=thin, bottom=thin)
center = Alignment(horizontal="center", vertical="center")

for c in ws[1]:
    c.fill = green
    c.font = Font(bold=True)
    c.alignment = center

for row in ws.iter_rows(min_row=1, max_row=ws.max_row, max_col=8):
    for c in row:
        c.border = border
        if c.column in (1, 2, 3, 6):
            c.alignment = center
for c in ws[last]:
    c.font = Font(bold=True)

widths = [8, 8, 8, 12, 10, 8, 12, 14]
for i, w in enumerate(widths, start=1):
    ws.column_dimensions[chr(64 + i)].width = w

out = os.path.join(DAILY, f"{date}-期初盘点表.xlsx")
wb.save(out)
print(f"已生成 {out}")
print(f"款数 {kinds} 件数 {total_qty} 金币 {total_gold} 万 ≈ ¥{total_rmb:.2f}（汇率 {price}/3000万）")
