import sqlite3
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()

# 1. 删掉今天的E票记录id=346
c.execute("DELETE FROM scan_log WHERE id=346 AND op='E'")
print("已删掉E票日结记录(id=346)")

# 2. 删掉今天日结生成的销售明细
c.execute("DELETE FROM sale_detail WHERE biz_date='2026-11-15'")
print("已清掉今日销售明细")

# 3. 把在售矩阵回滚到E票之前的状态（用id345 O票之后的数值）
# 先看下当前onsale的数
c.execute("SELECT name, qty FROM onsale ORDER BY name")
print("当前在售矩阵数：")
for row in c.fetchall():
    print(f"  {row[0]}: {row[1]}")

conn.commit()
conn.close()
print("\n✅ 日结已回滚，现在台账是日结前的状态，你说一下哪笔上架没入账，我改完再重新日结")
