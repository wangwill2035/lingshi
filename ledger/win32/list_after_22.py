import sqlite3
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()
print("=== 14号22:00之后所有扫码记录（按时间） ===")
c.execute("""SELECT id, biz_date, occ_time, op, detail, total_qty FROM scan_log 
WHERE occ_time >= '2026-11-14 22:00:00' ORDER BY occ_time""")
rows = c.fetchall()
for row in rows:
    print(f"id={row[0]}  业务日={row[1]}  实际时间={row[2]}  操作={row[3]}  件数={row[5]}")
    print(f"  明细：{row[4]}")
    print("---")
print(f"共{len(rows)}条")
