import sqlite3
raw = "1115221103O00000000000000000202010700000000"
ts = raw[:10]
op = raw[10]
qty = raw[11:]
pairs = [qty[i*2:i*2+2] for i in range(16)]
names = ["枫华戒","翠叶环","香木镯","芝兰佩","芙蓉戒","明月珰","翡玉镯","逸云佩","金麟绕","玉蝶翩","墨影扣","莲音玦","悦碧水","点星芒","花映月","相思染"]
total = 0
detail = []
for i in range(16):
    q = int(pairs[i])
    total += q
    if q>0:
        detail.append(f"{names[i]}×{q}")
print(f"扫码解析：时间={ts[:2]}-{ts[2:4]} {ts[4:6]}:{ts[6:8]}:{ts[8:10]}  操作=O上架  合计{total}件")
print(f"明细：{'+'.join(detail)}")

conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()
c.execute("SELECT id, occ_time, detail FROM scan_log WHERE raw_text=?", (raw,))
r = c.fetchone()
if r:
    print(f"\n✅ 数据库里有这条记录：id={r[0]}，时间={r[1]}")
else:
    print(f"\n❌ 数据库里没有这票！确实漏入账了，需要补录")
conn.close()
