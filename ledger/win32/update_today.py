import sqlite3, json, os
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()

# C票9:36盘点值
c_values = {
    "枫华戒":10, "翠叶环":3, "香木镯":10, "芝兰佩":10,
    "芙蓉戒":5, "明月珰":5, "翡玉镯":5, "逸云佩":5,
    "金麟绕":2, "玉蝶翩":6, "墨影扣":0, "莲音玦":4,
    "悦碧水":5, "点星芒":3, "花映月":1, "相思染":1
}
# 覆盖在售矩阵
for name, qty in c_values.items():
    c.execute("UPDATE onsale SET qty=? WHERE name=?", (qty, name))

# 写今天到现在的销售明细
sales = [
    ("枫华戒", "戒指", "60级", 2, 375, 750, "9.58%", 72),
    ("翠叶环", "耳饰", "60级", 3, 375, 1125, "9.58%", 108),
    ("香木镯", "手镯", "60级", 2, 185, 370, "18.38%", 68),
    ("芝兰佩", "佩饰", "60级", 10, 185, 1850, "18.38%", 340),
    ("明月珰", "耳饰", "80级", 1, 780, 780, "4.08%", 32),
    ("翡玉镯", "手镯", "80级", 1, 425, 425, "7.65%", 33),
    ("逸云佩", "佩饰", "80级", 5, 425, 2125, "7.65%", 163),
    ("金麟绕", "戒指", "100级", 3, 1500, 4500, "8.48%", 382),
    ("玉蝶翩", "耳饰", "100级", 1, 1450, 1450, "8.07%", 117),
    ("墨影扣", "手镯", "100级", 4, 580, 2320, "10.55%", 245),
    ("莲音玦", "佩饰", "100级", 7, 580, 4060, "10.55%", 428),
]
c.execute("DELETE FROM sale_detail WHERE biz_date='2026-11-16'")
for s in sales:
    c.execute("""INSERT INTO sale_detail (occ_time, biz_date, name, part, level, qty, unit_price, amount, profit_rate, profit)
    VALUES ('2026-11-16 09:36', '2026-11-16', ?, ?, ?, ?, ?, ?, ?, ?)""", s)

# 更新悬浮窗面板
total_qty = sum(s[3] for s in sales)
total_amt = sum(s[5] for s in sales)
appdata = os.getenv('APPDATA')
wd = os.path.join(appdata, 'MHXY-RateWidget')
today = {
    "date": "2026-11-16",
    "sale_qty": total_qty,
    "sale_wan": total_amt,
    "rmb": round(total_amt * 0.072, 0),
    "settled": False,
    "provisional": {"time":"09:36","sale_qty":total_qty,"sale_wan":total_amt,"rmb":round(total_amt*0.072,0)}
}
with open(os.path.join(wd,'today.json'),'w',encoding='utf-8') as f:
    json.dump(today, f, ensure_ascii=False, indent=2)

conn.commit()
conn.close()
print(f"✅ 更新完成：截止9:36，今日销售{total_qty}件，{total_amt}万，在售矩阵已覆盖为盘点值")
print("网页工作台刷新一下就能看到最新数据")
