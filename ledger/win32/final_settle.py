import sqlite3, json, os
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()
# 今天的回款（实际15号白天的）
c.execute("SELECT id, occ_time, amount, note FROM cash_log WHERE occ_time >= '2026-11-15 00:00:00' ORDER BY id")
print("今天实际回款：")
total_cash = 0
for row in c.fetchall():
    print(f"  id={row[0]}  时间={row[1]}  金额={row[2]}万  备注={row[3]}")
    total_cash += row[2]
print(f"合计回款：{total_cash}万")

sale_wan = 22365
unpaid = sale_wan - total_cash
print(f"未回款：{unpaid}万")

# 更新悬浮窗
appdata = os.getenv('APPDATA')
wd = os.path.join(appdata, 'MHXY-RateWidget')
today = {
    "date": "2026-11-15",
    "sale_qty": 50,
    "sale_wan": sale_wan,
    "rmb": round(sale_wan * 0.072, 0),
    "settled": True,
    "provisional": {"time": "21:52", "sale_qty":50, "sale_wan":sale_wan, "rmb":round(sale_wan*0.072,0)}
}
with open(os.path.join(wd, 'today.json'),'w',encoding='utf-8') as f:
    json.dump(today, f, ensure_ascii=False, indent=2)
cash = {"balance_wan": unpaid, "history": []}
with open(os.path.join(wd, 'cash.json'),'w',encoding='utf-8') as f:
    json.dump(cash, f, ensure_ascii=False, indent=2)
print("\n✅ 悬浮窗已更新")
conn.close()
