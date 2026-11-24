import sqlite3
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy_test.db')
c = conn.cursor()
c.execute("SELECT name FROM sqlite_master WHERE type='table'")
tables = [r[0] for r in c.fetchall()]
print('表列表：', tables)
for t in tables:
    c.execute(f"PRAGMA table_info({t})")
    cols = [r[1] for r in c.fetchall()]
    print(f'  {t} 列：{cols}')
    c.execute(f"SELECT * FROM {t} LIMIT 5")
    rows = c.fetchall()
    for row in rows:
        print(f'    {row}')
