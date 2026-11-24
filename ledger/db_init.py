import sqlite3, os

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mhxy.db")

def init_db():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()

    # 1. 扫码日志
    c.execute("""
    CREATE TABLE IF NOT EXISTS scan_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        biz_date TEXT NOT NULL,
        occ_time TEXT NOT NULL,
        op TEXT NOT NULL,
        raw_text TEXT NOT NULL,
        detail TEXT,
        total_qty INTEGER DEFAULT 0,
        signature TEXT UNIQUE
    )""")

    # 2. 入库明细（含上架转出的负数量）
    c.execute("""
    CREATE TABLE IF NOT EXISTS in_detail (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        occ_time TEXT NOT NULL,
        biz_date TEXT NOT NULL,
        name TEXT NOT NULL,
        part TEXT NOT NULL,
        level TEXT NOT NULL,
        qty INTEGER NOT NULL
    )""")

    # 3. 上架明细
    c.execute("""
    CREATE TABLE IF NOT EXISTS out_detail (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        occ_time TEXT NOT NULL,
        biz_date TEXT NOT NULL,
        name TEXT NOT NULL,
        part TEXT NOT NULL,
        level TEXT NOT NULL,
        qty INTEGER NOT NULL
    )""")

    # 4. 销售明细
    c.execute("""
    CREATE TABLE IF NOT EXISTS sale_detail (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        occ_time TEXT NOT NULL,
        biz_date TEXT NOT NULL,
        name TEXT NOT NULL,
        part TEXT NOT NULL,
        level TEXT NOT NULL,
        qty INTEGER NOT NULL,
        unit_price REAL NOT NULL,
        amount REAL NOT NULL,
        profit_rate TEXT,
        profit REAL
    )""")

    # 5. 回款记录
    c.execute("""
    CREATE TABLE IF NOT EXISTS cash_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        occ_time TEXT NOT NULL,
        biz_date TEXT NOT NULL,
        amount REAL NOT NULL,
        type TEXT DEFAULT '正常',
        note TEXT
    )""")

    # 6. 即时在售矩阵（16品类一行）
    c.execute("""
    CREATE TABLE IF NOT EXISTS onsale (
        name TEXT PRIMARY KEY,
        part TEXT NOT NULL,
        level TEXT NOT NULL,
        qty INTEGER DEFAULT 0
    )""")

    # 7. 即时库存矩阵
    c.execute("""
    CREATE TABLE IF NOT EXISTS storage (
        name TEXT PRIMARY KEY,
        part TEXT NOT NULL,
        level TEXT NOT NULL,
        qty INTEGER DEFAULT 0,
        unit_cost REAL
    )""")

    # 索引
    c.execute("CREATE INDEX IF NOT EXISTS idx_scan_biz ON scan_log(biz_date)")
    c.execute("CREATE INDEX IF NOT EXISTS idx_in_biz ON in_detail(biz_date)")
    c.execute("CREATE INDEX IF NOT EXISTS idx_out_biz ON out_detail(biz_date)")
    c.execute("CREATE INDEX IF NOT EXISTS idx_sale_biz ON sale_detail(biz_date)")
    c.execute("CREATE INDEX IF NOT EXISTS idx_cash_biz ON cash_log(biz_date)")

    conn.commit()
    conn.close()
    print(f"✅ 数据库初始化完成: {DB_PATH}")

if __name__ == "__main__":
    init_db()
