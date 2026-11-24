import sqlite3, os
DB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mhxy.db")
conn = sqlite3.connect(DB)
conn.row_factory = sqlite3.Row
cur = conn.cursor()

print("=== price_history 最新价格 ===")
cur.execute("""
SELECT name, price_wan, effective_date FROM price_history ph1
WHERE effective_date = (SELECT MAX(effective_date) FROM price_history ph2 WHERE ph2.name=ph1.name)
ORDER BY name
""")
for r in cur.fetchall():
    print(f'  {r["name"]:6s}  {r["price_wan"]:6.0f}万  生效日:{r["effective_date"]}')

print()
print("=== cost_history 最新成本 ===")
cur.execute("""
SELECT name, cost_wan, effective_date FROM cost_history ch1
WHERE effective_date = (SELECT MAX(effective_date) FROM cost_history ch2 WHERE ch2.name=ch1.name)
ORDER BY name
""")
for r in cur.fetchall():
    print(f'  {r["name"]:6s}  {r["cost_wan"]:6.0f}万  生效日:{r["effective_date"]}')

conn.close()
