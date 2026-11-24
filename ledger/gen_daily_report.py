import sqlite3, os, datetime

DB = r"C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰\mhxy.db"
REPORT_DIR = os.path.join(os.path.join(os.environ["USERPROFILE"], "Desktop"), "灵饰经营报表")

def gen_daily_report(biz_date=None):
    if not biz_date:
        biz_date = datetime.date.today().strftime("%Y-%m-%d")
    conn = sqlite3.connect(DB)
    c = conn.cursor()
    # 汇总数据
    c.execute("SELECT COALESCE(SUM(qty),0), COALESCE(SUM(amount),0) FROM sale_detail WHERE biz_date=?", (biz_date,))
    sale_qty, sale_amt = c.fetchone()
    c.execute("SELECT COALESCE(SUM(amount),0) FROM cash_log WHERE biz_date=?", (biz_date,))
    cash = c.fetchone()[0]
    unpaid = sale_amt - cash
    # 分品类销售
    c.execute("SELECT name, part, level, SUM(qty), SUM(amount) FROM sale_detail WHERE biz_date=? GROUP BY name ORDER BY level", (biz_date,))
    sales = c.fetchall()
    # 当日最后盘点在售数
    c.execute("SELECT SUM(qty) FROM onsale")
    stock = c.fetchone()[0]
    conn.close()

    md = f"# 梦幻西游灵饰经营日报 {biz_date}\n\n"
    md += "## 核心汇总\n"
    md += f"- 销售：{sale_qty}件，{sale_amt:.0f}万\n"
    md += f"- 回款：{cash:.0f}万\n"
    md += f"- 未回款：{unpaid:.0f}万\n"
    md += f"- 日结在售：{stock}件\n\n"
    md += "## 分品类销售明细\n"
    md += "| 品类 | 等级 | 部位 | 件数 | 金额(万) |\n"
    md += "|------|------|------|------|----------|\n"
    for s in sales:
        md += f"| {s[0]} | {s[2]} | {s[1]} | {s[3]} | {s[4]:.0f} |\n"
    md += f"\n*报表生成时间：{datetime.datetime.now().strftime('%Y-%m-%d %H:%M')}*\n"

    out = os.path.join(REPORT_DIR, f"日报-{biz_date}.md")
    with open(out, "w", encoding="utf-8") as f:
        f.write(md)
    print(f"✅ 日报已生成：{out}")
    return out

if __name__ == "__main__":
    gen_daily_report()
