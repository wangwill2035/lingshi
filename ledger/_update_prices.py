import sqlite3, os, datetime

DB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mhxy.db")
print("DB路径:", DB)
print("文件存在:", os.path.exists(DB))

# 最新售价（2026-11-17生效）
NEW_PRICES = {
    "枫华戒": 330, "翠叶环": 330, "香木镯": 175, "芝兰佩": 175,
    "芙蓉戒": 770, "明月珰": 740, "翡玉镯": 400, "逸云佩": 400,
    "金麟绕": 1430, "玉蝶翩": 1400, "墨影扣": 550, "莲音玦": 550,
    "悦碧水": 2450, "点星芒": 2340, "花映月": 945, "相思染": 985,
}

today = datetime.date.today().strftime("%Y-%m-%d")
now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")

conn = sqlite3.connect(DB)
cur = conn.cursor()

# 查当前最新价格
cur.execute("""
SELECT name, price_wan FROM price_history ph1
WHERE effective_date = (SELECT MAX(effective_date) FROM price_history ph2 WHERE ph2.name=ph1.name)
""")
current = {r[0]: r[1] for r in cur.fetchall()}

changed = 0
for name, new_p in NEW_PRICES.items():
    old_p = current.get(name, 0)
    if abs(old_p - new_p) > 0.01:
        cur.execute("""
            INSERT INTO price_history(name, price_wan, effective_date, created_at)
            VALUES(?,?,?,?)
        """, (name, new_p, today, now))
        print(f"  {name:6s}  {old_p:6.0f} → {new_p:6.0f}万  ✓")
        changed += 1

conn.commit()
conn.close()
print(f"\n完成：共更新 {changed} 条价格，生效日 {today}")
