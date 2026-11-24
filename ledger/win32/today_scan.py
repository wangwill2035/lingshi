import sqlite3
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()
c.execute("SELECT id, occ_time, op, detail, total_qty FROM scan_log WHERE occ_time >= '2026-11-16 00:00:00' ORDER BY id")
rows = c.fetchall()
print("今日所有扫码记录：")
for r in rows:
    print(f"  id={r[0]} 时间={r[1]} 操作={r[2]} 合计{r[4]}件 明细={r[3]}")
conn.close()
