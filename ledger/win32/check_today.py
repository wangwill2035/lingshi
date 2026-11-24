import sqlite3
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()
print("=== 今天(2026-11-14)的扫码记录 ===")
c.execute("SELECT id, biz_date, occ_time, op, detail, total_qty FROM scan_log WHERE biz_date='2026-11-14' ORDER BY id")
for row in c.fetchall():
    print(row)

print("\n=== 今天的销售明细汇总 ===")
c.execute("SELECT SUM(qty), SUM(amount), SUM(profit) FROM sale_detail WHERE biz_date='2026-11-14'")
print(c.fetchone())

print("\n=== 今天的回款记录 ===")
c.execute("SELECT id, occ_time, amount, type, note FROM cash_log WHERE biz_date='2026-11-14'")
for row in c.fetchall():
    print(row)
