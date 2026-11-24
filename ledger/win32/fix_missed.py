import sqlite3
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()

# 补录scan_log
c.execute("""INSERT INTO scan_log (biz_date, occ_time, op, raw_text, detail, total_qty)
VALUES ('2026-11-15', '2026-11-15 22:11:03', 'O', '1115221103O00000000000000000202010700000000', '金麟绕×2+玉蝶翩×2+墨影扣×1+莲音玦×7', 12)""")
print("已补录扫码日志")

# 上架操作：库存减N，在售加N
# 金麟绕(100戒)：库存-2，在售+2
c.execute("UPDATE storage SET qty=qty-2 WHERE name='金麟绕'")
c.execute("UPDATE onsale SET qty=qty+2 WHERE name='金麟绕'")
# 玉蝶翩(100耳)：库存-2，在售+2
c.execute("UPDATE storage SET qty=qty-2 WHERE name='玉蝶翩'")
c.execute("UPDATE onsale SET qty=qty+2 WHERE name='玉蝶翩'")
# 墨影扣(100镯)：库存-1，在售+1
c.execute("UPDATE storage SET qty=qty-1 WHERE name='墨影扣'")
c.execute("UPDATE onsale SET qty=qty+1 WHERE name='墨影扣'")
# 莲音玦(100佩)：库存-7，在售+7
c.execute("UPDATE storage SET qty=qty-7 WHERE name='莲音玦'")
c.execute("UPDATE onsale SET qty=qty+7 WHERE name='莲音玦'")

# 写out_detail明细
c.execute("INSERT INTO out_detail (occ_time, biz_date, name, part, level, qty) VALUES ('2026-11-15 22:11', '2026-11-15', '金麟绕', '戒指', '100级', 2)")
c.execute("INSERT INTO out_detail (occ_time, biz_date, name, part, level, qty) VALUES ('2026-11-15 22:11', '2026-11-15', '玉蝶翩', '耳饰', '100级', 2)")
c.execute("INSERT INTO out_detail (occ_time, biz_date, name, part, level, qty) VALUES ('2026-11-15 22:11', '2026-11-15', '墨影扣', '手镯', '100级', 1)")
c.execute("INSERT INTO out_detail (occ_time, biz_date, name, part, level, qty) VALUES ('2026-11-15 22:11', '2026-11-15', '莲音玦', '佩饰', '100级', 7)")

conn.commit()
print("✅ 补录完成：12件上架已入账，库存和在售矩阵已更新")
print("当前在售矩阵：")
c.execute("SELECT name, qty FROM onsale ORDER BY name")
for row in c.fetchall():
    print(f"  {row[0]}: {row[1]}")
conn.close()
