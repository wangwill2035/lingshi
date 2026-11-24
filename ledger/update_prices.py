import sqlite3, datetime
conn = sqlite3.connect('mhxy.db')
cur = conn.cursor()
today = datetime.date.today().strftime("%Y-%m-%d")
now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
# 最新价格表（2026-11-17执行）
new_prices = {
    "枫华戒": 330, "翠叶环": 330, "香木镯": 175, "芝兰佩": 175,
    "芙蓉戒": 770, "明月珰": 740, "翡玉镯": 400, "逸云佩": 400,
    "金麟绕": 1430, "玉蝶翩": 1400, "墨影扣": 550, "莲音玦": 550,
    "悦碧水": 2450, "点星芒": 2340, "花映月": 945, "相思染": 985,
}
for name, p in new_prices.items():
    cur.execute("INSERT INTO price_history(name, price_wan, effective_date, created_at) VALUES(?,?,?,?)",
                (name, p, today, now))
conn.commit()
# 验证
cur.execute('SELECT name, price_wan FROM price_history ph1 WHERE effective_date=(SELECT MAX(effective_date) FROM price_history ph2 WHERE ph2.name=ph1.name) ORDER BY id')
print("更新后最新售价：")
for r in cur.fetchall():
    print(f"{r[0]}: {r[1]}万")
conn.close()
