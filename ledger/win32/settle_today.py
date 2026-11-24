import sqlite3, json, os
conn = sqlite3.connect(r'C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db')
c = conn.cursor()

# 汇总今天的销售
c.execute("SELECT SUM(qty), SUM(amount) FROM sale_detail WHERE biz_date='2026-11-14'")
sale_qty, sale_wan = c.fetchone()
print(f"今日销售：{sale_qty}件，{sale_wan}万")

# 汇总今天回款
c.execute("SELECT SUM(amount) FROM cash_log WHERE biz_date='2026-11-14'")
cash_wan = c.fetchone()[0]
print(f"今日回款：{cash_wan}万")
unpaid = sale_wan - cash_wan
print(f"未回款：{unpaid}万")

# 更新悬浮窗面板
appdata = os.getenv('APPDATA')
widget_dir = os.path.join(appdata, 'MHXY-RateWidget')
os.makedirs(widget_dir, exist_ok=True)

# 写today.json
today = {
    "date": "2026-11-14",
    "sale_qty": sale_qty,
    "sale_wan": round(sale_wan, 0),
    "rmb": round(sale_wan * 0.072, 0),
    "settled": True,
    "provisional": {
        "time": "21:57",
        "sale_qty": sale_qty,
        "sale_wan": round(sale_wan, 0),
        "rmb": round(sale_wan * 0.072, 0)
    }
}
with open(os.path.join(widget_dir, 'today.json'), 'w', encoding='utf-8') as f:
    json.dump(today, f, ensure_ascii=False, indent=2)

# 写cash.json
cash = {
    "balance_wan": unpaid,
    "history": [
        {"time": "23:47", "wan": 12400},
        {"time": "07:10", "wan": 8792},
        {"time": "18:03", "wan": 10293}
    ]
}
with open(os.path.join(widget_dir, 'cash.json'), 'w', encoding='utf-8') as f:
    json.dump(cash, f, ensure_ascii=False, indent=2)

print("✅ 悬浮窗面板已更新，重启面板就生效了")
