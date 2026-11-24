import sqlite3
conn = sqlite3.connect('mhxy.db')
conn.row_factory = sqlite3.Row
cur = conn.cursor()
cur.execute('SELECT name, price_wan, effective_date FROM price_history ph1 WHERE effective_date=(SELECT MAX(effective_date) FROM price_history ph2 WHERE ph2.name=ph1.name) ORDER BY id')
print('=== 当前售价 ===')
for r in cur.fetchall():
    print(f"{r['name']}: {r['price_wan']}万 (生效日:{r['effective_date']})")
cur.execute('SELECT name, cost_wan, effective_date FROM cost_history ch1 WHERE effective_date=(SELECT MAX(effective_date) FROM cost_history ch2 WHERE ch2.name=ch1.name) ORDER BY id')
print('\n=== 当前成本 ===')
for r in cur.fetchall():
    print(f"{r['name']}: {r['cost_wan']}万")
conn.close()
