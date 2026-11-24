import sqlite3
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()
print("=== 2026-11-15 今日全部扫码记录 ===")
c.execute("SELECT id, occ_time, op, detail, total_qty FROM scan_log WHERE biz_date='2026-11-15' ORDER BY id")
rows = c.fetchall()
for row in rows:
    print(f"id={row[0]}  时间={row[1]}  操作={row[2]}  件数={row[4]}")
    print(f"    明细：{row[3]}")
    print("---")
print(f"\n今日总记录数：{len(rows)}条")

print("\n=== 今日回款记录 ===")
c.execute("SELECT id, occ_time, amount, type, note FROM cash_log WHERE biz_date='2026-11-15'")
for row in c.fetchall():
    print(row)

print("\n=== 今日销售汇总 ===")
c.execute("SELECT SUM(qty), SUM(amount) FROM sale_detail WHERE biz_date='2026-11-15'")
print(c.fetchone())
