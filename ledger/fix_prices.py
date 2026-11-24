import sqlite3, datetime
conn = sqlite3.connect('mhxy.db')
cur = conn.cursor()
today = datetime.date.today().strftime("%Y-%m-%d")
# 删除我刚才错误添加的今天的价格记录
cur.execute("DELETE FROM price_history WHERE effective_date=?", (today,))
# 正确的最新价格表
correct_prices = {
    "枫华戒": 375, "翠叶环": 375, "香木镯": 185, "芝兰佩": 185,
    "芙蓉戒": 800, "明月珰": 780, "翡玉镯": 425, "逸云佩": 425,
    "金麟绕": 1500, "玉蝶翩": 1450, "墨影扣": 590, "莲音玦": 590,
    "悦碧水": 2450, "点星芒": 2350, "花映月": 945, "相思染": 985,
}
now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
for name, p in correct_prices.items():
    cur.execute("INSERT INTO price_history(name, price_wan, effective_date, created_at) VALUES(?,?,?,?)",
                (name, p, today, now))
conn.commit()
# 验证
cur.execute('SELECT name, price_wan FROM price_history ph1 WHERE effective_date=(SELECT MAX(effective_date) FROM price_history ph2 WHERE ph2.name=ph1.name) ORDER BY id')
print("已恢复正确价格：")
for r in cur.fetchall():
    print(f"{r[0]}: {r[1]}万")
conn.close()
