import sqlite3, json, os
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()

# 截图里正确的销售明细
correct_sales = [
    ("枫华戒", "戒指", "60级", 11, 375, 4125, "9.58%", 395),
    ("翠叶环", "耳饰", "60级", 4, 375, 1500, "9.58%", 144),
    ("香木镯", "手镯", "60级", 6, 185, 1110, "18.38%", 204),
    ("芝兰佩", "佩饰", "60级", 10, 185, 1850, "18.38%", 340),
    ("金麟绕", "戒指", "100级", 5, 1500, 7500, "8.48%", 636),
    ("玉蝶翩", "耳饰", "100级", 2, 1450, 2900, "8.07%", 234),
    ("墨影扣", "手镯", "100级", 17, 580, 9860, "10.55%", 1040),
    ("莲音玦", "佩饰", "100级", 7, 580, 4060, "10.55%", 428),
]

# 删掉旧错的
c.execute("DELETE FROM sale_detail WHERE biz_date='2026-11-15'")
# 写入正确的
for s in correct_sales:
    c.execute("""INSERT INTO sale_detail (occ_time, biz_date, name, part, level, qty, unit_price, amount, profit_rate, profit)
    VALUES ('2026-11-15 21:52', '2026-11-15', ?, ?, ?, ?, ?, ?, ?, ?)""", s)

total_qty = sum(s[3] for s in correct_sales)
total_amt = sum(s[5] for s in correct_sales)
print(f"✅ 修正后今日销售：{total_qty}件，{total_amt}万")

# 回款：今天实际33746万
cash = 33746
unpaid = total_amt - cash
print(f"今日回款：{cash}万，未回款：{unpaid}万（负的是点卡/以前欠款）")

# 更新悬浮窗
appdata = os.getenv('APPDATA')
wd = os.path.join(appdata, 'MHXY-RateWidget')
today = {
    "date": "2026-11-15",
    "sale_qty": total_qty,
    "sale_wan": total_amt,
    "rmb": round(total_amt * 0.072, 0),
    "settled": True,
    "provisional": {"time":"21:52","sale_qty":total_qty,"sale_wan":total_amt,"rmb":round(total_amt*0.072,0)}
}
with open(os.path.join(wd,'today.json'),'w',encoding='utf-8') as f:
    json.dump(today, f, ensure_ascii=False, indent=2)
cashj = {"balance_wan": unpaid, "history":[]}
with open(os.path.join(wd,'cash.json'),'w',encoding='utf-8') as f:
    json.dump(cashj, f, ensure_ascii=False, indent=2)

conn.commit()
conn.close()
print("✅ 数据库和悬浮窗都已修正为正确数")
