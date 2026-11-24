import sqlite3
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()
print("=== 2026-11-14 全部扫码记录（按时间顺序） ===")
c.execute("SELECT id, occ_time, op, detail, total_qty FROM scan_log WHERE biz_date='2026-11-14' ORDER BY id")
rows = c.fetchall()
for row in rows:
    print(f"id={row[0]}  时间={row[1]}  操作={row[2]}  件数={row[4]}")
    print(f"    明细：{row[3]}")
    print("---")
print(f"\n总记录数：{len(rows)}条")
