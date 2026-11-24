import http.server, json, os, subprocess, datetime, sqlite3, sys, shutil

PORT = 8000
# 打包后 onefile 解压到临时目录，数据库/前端必须定位到 exe 所在的真实目录
if getattr(sys, "frozen", False):
    APP_DIR = os.path.dirname(sys.executable)
else:
    APP_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(APP_DIR, "mhxy.db")
HTML_PATH = os.path.join(APP_DIR, "工作台.html")

# ===== 内置扫码解析（原 parse_qr.py 契约，43 字符，勿改）=====
# 品类主表种子：idx 即二维码 32 位数量段的槽位顺序，属扫码契约，不可重排
CATEGORY_SEED = [
    (1,  "枫华戒", "戒指", 60), (2,  "翠叶环", "耳饰", 60),
    (3,  "香木镯", "手镯", 60), (4,  "芝兰佩", "佩饰", 60),
    (5,  "芙蓉戒", "戒指", 80), (6,  "明月珰", "耳饰", 80),
    (7,  "翡玉镯", "手镯", 80), (8,  "逸云佩", "佩饰", 80),
    (9,  "金麟绕", "戒指", 100), (10, "玉蝶翩", "耳饰", 100),
    (11, "墨影扣", "手镯", 100), (12, "莲音玦", "佩饰", 100),
    (13, "悦碧水", "戒指", 120), (14, "点星芒", "耳饰", 120),
    (15, "花映月", "手镯", 120), (16, "相思染", "佩饰", 120),
]
# 兼容旧引用：名称/等级列表
NAMES = [(name, level) for _, name, _, level in CATEGORY_SEED]
PARTS = ("戒指", "耳饰", "手镯", "佩饰")
LEN = 43
OP_NAMES = {"I": "入库", "O": "上架", "C": "中途盘点", "E": "日结盘点"}
OP_REGISTER = ("I", "O")
LEVEL_ROWS = {60: 2, 80: 3, 100: 4, 120: 5}
PART_COLS = {"戒指": "B", "耳饰": "C", "手镯": "D", "佩饰": "E"}
# 展示用默认单价（万）；实际入库金额以数据库 price_history 最新价为准
DEFAULT_PARSE_PRICES = {
    60: {"戒指": 375, "耳饰": 375, "手镯": 185, "佩饰": 185},
    80: {"戒指": 800, "耳饰": 780, "手镯": 425, "佩饰": 425},
    100: {"戒指": 1500, "耳饰": 1450, "手镯": 580, "佩饰": 580},
    120: {"戒指": 2450, "耳饰": 2350, "手镯": 945, "佩饰": 985},
}

def _parse_item(i, pair, qty, cat):
    name, part, level = cat["name"], cat["part"], cat["level"]
    unit_price = DEFAULT_PARSE_PRICES.get(level, {}).get(part, 0)
    return {
        "idx": i + 1, "name": name, "level": level, "part": part,
        "display": "%s%d" % (name, level), "pair": pair, "qty": qty,
        "unit_price_wan": unit_price, "amount_wan": qty * unit_price,
        "matrix_cell": "%s%d" % (PART_COLS[part], LEVEL_ROWS[level]),
    }

def parse_line(raw, today):
    s = (raw or "").strip()
    if not s:
        return {"ok": False, "error": "空行", "raw": raw}
    if len(s) != LEN:
        return {"ok": False, "error": "长度 %d ≠ %d" % (len(s), LEN), "raw": raw}
    ts, op, qty = s[:10], s[10:11], s[11:]
    if not ts.isdigit():
        return {"ok": False, "error": "时间戳含非数字", "raw": raw}
    if op not in OP_NAMES:
        return {"ok": False, "error": "操作符 '%s' 非 I/O/C/E" % op, "raw": raw}
    if not qty.isdigit() or len(qty) != 32:
        return {"ok": False, "error": "数量段非 32 位数字", "raw": raw}
    pairs = [qty[i * 2:i * 2 + 2] for i in range(16)]
    cats = get_categories()
    items = [_parse_item(i, pairs[i], int(pairs[i]), cats[i]) for i in range(16)]
    total = sum(it["qty"] for it in items)
    if total == 0:
        return {"ok": False, "error": "全零记录（16 件均为 00）无效", "raw": raw}
    mm, dd, hh, mi, ss = (int(ts[0:2]), int(ts[2:4]), int(ts[4:6]), int(ts[6:8]), int(ts[8:10]))
    try:
        date = today.replace(month=mm, day=dd)
    except ValueError:
        return {"ok": False, "error": "无效日期 %02d-%02d" % (mm, dd), "raw": raw}
    date_s = date.strftime("%Y-%m-%d")
    time_s = "%02d:%02d:%02d" % (hh, mi, ss)
    nonzero = [it for it in items if it["qty"] > 0]
    total_wan = sum(it["amount_wan"] for it in nonzero)
    detail = "+".join("%s×%d" % (it["name"], it["qty"]) for it in nonzero)
    rows = [{
        "date": date_s, "time": time_s, "name": it["name"], "part": it["part"],
        "level_text": "%d级" % it["level"], "qty": it["qty"],
        "unit_price_wan": it["unit_price_wan"], "amount_wan": it["amount_wan"],
    } for it in nonzero]
    matrix_delta = {}
    for it in nonzero:
        n = it["qty"]
        if op == "I":
            matrix_delta[it["matrix_cell"]] = {"stock": n, "onsale": 0}
        elif op == "O":
            matrix_delta[it["matrix_cell"]] = {"stock": -n, "onsale": n}
    r = {
        "ok": True, "raw": raw, "date": date_s, "time": time_s, "op": op,
        "op_name": OP_NAMES[op],
        "flow": "register" if op in OP_REGISTER else "check",
        "items": items, "nonzero": nonzero, "total": total, "total_wan": total_wan,
        "detail": detail, "rows": rows, "matrix_delta": matrix_delta,
        "signature": op + qty, "cross_date": date_s != today.strftime("%Y-%m-%d"),
    }
    if op not in OP_REGISTER:
        r["pending"] = "盘点票：16 个数为当时在售实盘值。"
        r.pop("matrix_delta", None)
    return r

DEFAULT_PRICES = {
    "枫华戒": 330, "翠叶环": 330, "香木镯": 175, "芝兰佩": 175,
    "芙蓉戒": 770, "明月珰": 740, "翡玉镯": 400, "逸云佩": 400,
    "金麟绕": 1430, "玉蝶翩": 1400, "墨影扣": 550, "莲音玦": 550,
    "悦碧水": 2450, "点星芒": 2340, "花映月": 945, "相思染": 985,
}
DEFAULT_COSTS = {
    "枫华戒":342,"翠叶环":342,"香木镯":157,"芝兰佩":157,
    "芙蓉戒":762,"明月珰":742,"翡玉镯":387,"逸云佩":387,
    "金麟绕":1357,"玉蝶翩":1317,"墨影扣":517,"莲音玦":517,
    "悦碧水":2315,"点星芒":2165,"花映月":845,"相思染":885
}

def db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn

def _backup_db():
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    bak = DB_PATH + ".bak-rollback-" + ts
    shutil.copy2(DB_PATH, bak)
    return os.path.basename(bak)

def _rollback_plan(cur, scan_id):
    """只读预检：校验票型、反推撤后库存/在售、检查负库存/负在售、识别后续票。返回 plan 或 {err, code}。"""
    cur.execute("SELECT * FROM scan_log WHERE id=?", (scan_id,))
    log = cur.fetchone()
    if not log:
        return {"err": "记录不存在", "code": 404}
    op = log["op"]
    if op not in ("I", "O"):
        return {"err": "暂不支持网页撤销（C/E 盘点票请联系助手核对库表）", "code": 400, "op": op}
    occ, bdate = log["occ_time"], log["biz_date"]
    items = []
    if op == "I":
        rows = cur.execute("SELECT name,part,level,qty FROM in_detail WHERE occ_time=? AND biz_date=? AND qty>0 ORDER BY id", (occ, bdate)).fetchall()
        for r in rows:
            cur.execute("SELECT qty FROM storage WHERE name=?", (r["name"],))
            row = cur.fetchone()
            sb = row["qty"] if row else 0
            items.append({"name": r["name"], "part": r["part"], "level": r["level"], "qty": r["qty"],
                           "stock_before": sb, "stock_after": sb - r["qty"], "onsale_before": None, "onsale_after": None})
    else:
        rows = cur.execute("SELECT name,part,level,qty FROM out_detail WHERE occ_time=? AND biz_date=? ORDER BY id", (occ, bdate)).fetchall()
        for r in rows:
            cur.execute("SELECT qty FROM storage WHERE name=?", (r["name"],))
            srow = cur.fetchone()
            sb = srow["qty"] if srow else 0
            cur.execute("SELECT qty FROM onsale WHERE name=?", (r["name"],))
            orow = cur.fetchone()
            ob = orow["qty"] if orow else 0
            items.append({"name": r["name"], "part": r["part"], "level": r["level"], "qty": r["qty"],
                           "stock_before": sb, "stock_after": sb + r["qty"], "onsale_before": ob, "onsale_after": ob - r["qty"]})
    if not items:
        return {"err": "未找到该票对应明细，无法撤销", "code": 400}
    for it in items:
        if op == "I" and it["stock_after"] < 0:
            return {"err": "库存不足：撤后 %s 库存为 %d（被后续上架依赖），不能撤销" % (it["name"], it["stock_after"]), "code": 400}
        if op == "O" and it["onsale_after"] < 0:
            return {"err": "在售不足：撤后 %s 在售为 %d，不能撤销" % (it["name"], it["onsale_after"]), "code": 400}
    later = cur.execute("SELECT COUNT(*) n FROM scan_log WHERE id>?", (scan_id,)).fetchone()["n"]
    return {"log": log, "op": op, "occ": occ, "bdate": bdate, "items": items,
            "has_later": later > 0, "later_count": later}

def init_price():
    c = db()
    cur = c.cursor()
    cur.execute("""CREATE TABLE IF NOT EXISTS price_history(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL,
        price_wan REAL NOT NULL,
        effective_date TEXT NOT NULL,
        created_at TEXT NOT NULL
    )""")
    cur.execute("""CREATE TABLE IF NOT EXISTS cost_history(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL,
        cost_wan REAL NOT NULL,
        effective_date TEXT NOT NULL,
        created_at TEXT NOT NULL
    )""")
    # 待处理扫码表：扫码后先存这里，用户确认后再入账
    cur.execute("""CREATE TABLE IF NOT EXISTS pending_scan(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        raw_text TEXT NOT NULL,
        op TEXT NOT NULL,
        date TEXT NOT NULL,
        time TEXT NOT NULL,
        detail TEXT NOT NULL,
        total INTEGER NOT NULL,
        preview_json TEXT NOT NULL,
        created_at TEXT NOT NULL
    )""")
    today = datetime.date.today().strftime("%Y-%m-%d")
    for name, p in DEFAULT_PRICES.items():
        cur.execute("SELECT COUNT(*) cnt FROM price_history WHERE name=?", (name,))
        if cur.fetchone()["cnt"] == 0:
            cur.execute("INSERT INTO price_history(name, price_wan, effective_date, created_at) VALUES(?,?,?,?)",
                        (name, p, today, datetime.datetime.now().strftime("%Y-%m-%d %H:%M")))
    for name, cst in DEFAULT_COSTS.items():
        cur.execute("SELECT COUNT(*) cnt FROM cost_history WHERE name=?", (name,))
        if cur.fetchone()["cnt"] == 0:
            cur.execute("INSERT INTO cost_history(name, cost_wan, effective_date, created_at) VALUES(?,?,?,?)",
                        (name, cst, today, datetime.datetime.now().strftime("%Y-%m-%d %H:%M")))
    c.commit()
    c.close()

def init_category():
    c = db()
    cur = c.cursor()
    cur.execute("""CREATE TABLE IF NOT EXISTS category(
        idx INTEGER PRIMARY KEY,
        name TEXT NOT NULL UNIQUE,
        part TEXT NOT NULL,
        level INTEGER NOT NULL
    )""")
    cur.execute("SELECT COUNT(*) n FROM category")
    if cur.fetchone()["n"] == 0:
        cur.executemany("INSERT INTO category(idx,name,part,level) VALUES(?,?,?,?)", CATEGORY_SEED)
    c.commit()
    c.close()

def get_categories():
    c = db()
    cur = c.cursor()
    rows = cur.execute("SELECT idx,name,part,level FROM category ORDER BY idx").fetchall()
    c.close()
    return [{"idx": r["idx"], "name": r["name"], "part": r["part"], "level": r["level"]} for r in rows]

def get_prices():
    c = db()
    cur = c.cursor()
    cur.execute("SELECT name, price_wan FROM price_history ph1 WHERE effective_date = (SELECT MAX(effective_date) FROM price_history ph2 WHERE ph2.name=ph1.name)")
    prices = {row["name"]: row["price_wan"] for row in cur.fetchall()}
    c.close()
    return prices

def get_costs():
    c = db()
    cur = c.cursor()
    cur.execute("SELECT name, cost_wan FROM cost_history ch1 WHERE effective_date = (SELECT MAX(effective_date) FROM cost_history ch2 WHERE ch2.name=ch1.name)")
    costs = {row["name"]: row["cost_wan"] for row in cur.fetchall()}
    c.close()
    return costs

def biz_date(dt):
    # 业务规则：日结分界点是扫 E 票完成，不是凌晨 0 点，也不是 22 点自动跳天。
    # 最近一张 E 票之后进入次日营业周期：若还停留在 E 票当天日历日（晚上未跨凌晨），
    # E 已结则业务日取 E 票日期的次日；已跨凌晨则按系统当天日期。
    today = dt.date()
    e_date = None
    c = db()
    try:
        cur = c.cursor()
        row = cur.execute("SELECT biz_date FROM scan_log WHERE op='E' ORDER BY id DESC LIMIT 1").fetchone()
        if row and row["biz_date"]:
            e_date = datetime.datetime.strptime(row["biz_date"], "%Y-%m-%d").date()
    finally:
        c.close()
    if e_date and today <= e_date:
        return (e_date + datetime.timedelta(days=1)).strftime("%Y-%m-%d")
    return today.strftime("%Y-%m-%d")

def parse_qr(text):
    # 内置同进程解析，不再拉起外部 Python 子进程（打包后无解释器依赖）
    try:
        return parse_line(text, datetime.datetime.now())
    except Exception as e:
        return {"error": "解析异常: %s" % e}

def today_summary(biz):
    c = db()
    cur = c.cursor()
    cur.execute("SELECT COALESCE(SUM(qty),0) qty, COALESCE(SUM(amount),0) amt FROM sale_detail WHERE biz_date=?", (biz,))
    s = cur.fetchone()
    cur.execute("SELECT COALESCE(SUM(amount),0) cash FROM cash_log WHERE biz_date=?", (biz,))
    ch = cur.fetchone()
    cur.execute("SELECT COALESCE(SUM(qty),0) restock FROM out_detail WHERE biz_date=?", (biz,))
    rk = cur.fetchone()
    sale_wan = s["amt"]
    cash_wan = ch["cash"]
    c.close()
    return {
        "date": biz,
        "sale_qty": s["qty"],
        "sale_wan": sale_wan,
        "cash_wan": cash_wan,
        "balance_wan": max(0, sale_wan - cash_wan),
        "restock_cnt": rk["restock"],
    }

class Handler(http.server.BaseHTTPRequestHandler):
    def send_json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", len(body))
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET,POST,OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            html_path = HTML_PATH
            with open(html_path, "r", encoding="utf-8") as f:
                body = f.read().encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", len(body))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/data":
            now = datetime.datetime.now()
            biz = biz_date(now)
            c = db()
            cur = c.cursor()
            cur.execute("SELECT * FROM onsale ORDER BY rowid")
            onsale = [dict(x) for x in cur.fetchall()]
            cur.execute("SELECT * FROM storage ORDER BY rowid")
            storage = [dict(x) for x in cur.fetchall()]
            summary = today_summary(biz)
            prices = get_prices()
            costs = get_costs()
            # 今日销售矩阵：按品类汇总qty和金额
            cur.execute("SELECT name, SUM(qty) qty, SUM(amount) amt FROM sale_detail WHERE biz_date=? GROUP BY name", (biz,))
            sale_matrix = {}
            for row in cur.fetchall():
                sale_matrix[row["name"]] = {"qty": row["qty"], "amt": row["amt"]}
            # 今日回款列表
            cur.execute("SELECT occ_time, amount FROM cash_log WHERE biz_date=? ORDER BY occ_time", (biz,))
            cash_list = [dict(x) for x in cur.fetchall()]
            # 今日已入账的扫码记录
            cur.execute("SELECT id, occ_time, biz_date, op, detail, total_qty FROM scan_log WHERE biz_date=? ORDER BY id DESC", (biz,))
            scan_log = [dict(x) for x in cur.fetchall()]
            # 待处理的扫码记录（存在数据库里，刷新不丢）
            cur.execute("SELECT id, op, date, time, detail, total FROM pending_scan ORDER BY id DESC")
            pending = [dict(x) for x in cur.fetchall()]
            # 昨日日结汇总：取最近一个非今天的 E 日结票，动态汇总销售/回款/盘点在售
            yesterday = None
            cur.execute("SELECT biz_date, total_qty FROM scan_log WHERE op='E' AND biz_date < ? ORDER BY id DESC LIMIT 1", (biz,))
            yrow = cur.fetchone()
            if yrow:
                yd = yrow["biz_date"]
                cur.execute("SELECT COALESCE(SUM(qty),0) q, COALESCE(SUM(amount),0) a FROM sale_detail WHERE biz_date=?", (yd,))
                ys = cur.fetchone()
                cur.execute("SELECT COALESCE(SUM(amount),0) cash FROM cash_log WHERE biz_date=?", (yd,))
                yc = cur.fetchone()
                yesterday = {"date": yd, "sale_qty": ys["q"], "sale_wan": ys["a"],
                             "cash_wan": yc["cash"], "onsale_qty": yrow["total_qty"]}
            c.close()
            self.send_json(200, {"today": summary, "stock": onsale, "storage": storage, "prices": prices, "costs": costs, "sale_matrix": sale_matrix, "cash_list": cash_list, "scan_log": scan_log, "pending": pending, "yesterday": yesterday, "categories": get_categories()})
        elif self.path == "/api/report/daily":
            # 当日日报数据，含毛利润
            c = db()
            cur = c.cursor()
            cur.execute("SELECT biz_date FROM sale_detail ORDER BY id DESC LIMIT 1")
            row = cur.fetchone()
            biz = row[0] if row else datetime.date.today().strftime("%Y-%m-%d")
            costs = get_costs()
            prices = get_prices()
            cur.execute("SELECT COALESCE(SUM(qty),0), COALESCE(SUM(amount),0) FROM sale_detail WHERE biz_date=?", (biz,))
            sq, sa = cur.fetchone()
            cats = get_categories()
            cur.execute("SELECT name, SUM(qty) FROM sale_detail WHERE biz_date=? GROUP BY name", (biz,))
            cost_sum = 0
            profit_sum = 0
            raw_items = {}
            for r in cur.fetchall():
                raw_items[r[0]] = r[1]
            items = []
            for cat in cats:
                name, part, level = cat["name"], cat["part"], cat["level"]
                qty = raw_items.get(name, 0)
                cost = costs.get(name, 0)
                price = prices.get(name, 0)
                amt = price * qty
                cost_sum += cost * qty
                profit = amt - cost*qty
                profit_sum += profit
                items.append({"name":name, "part":part, "level":"%d级" % level, "qty":qty, "amount":amt, "cost":cost*qty, "profit":profit})
            cur.execute("SELECT COALESCE(SUM(amount),0) FROM cash_log WHERE biz_date=?", (biz,))
            cash = cur.fetchone()[0]
            c.close()
            profit_rate = (profit_sum/sa*100) if sa>0 else 0
            self.send_json(200, {"date": biz, "sale_qty": sq, "sale_amt": sa, "cash": cash, "unpaid": sa-cash, "cost_sum":cost_sum, "profit_sum":profit_sum, "profit_rate":profit_rate, "items": items})
        elif self.path == "/api/report/weekly":
            # 近7天周报数据，含毛利润
            c = db()
            cur = c.cursor()
            today = datetime.date.today()
            start = (today - datetime.timedelta(days=6)).strftime("%Y-%m-%d")
            costs = get_costs()
            prices = get_prices()
            cur.execute("SELECT biz_date, SUM(qty), SUM(amount) FROM sale_detail WHERE biz_date>=? GROUP BY biz_date ORDER BY biz_date", (start,))
            days = []
            total_cost = 0
            for r in cur.fetchall():
                d, qty, amt = r
                cur2 = c.cursor()
                cur2.execute("SELECT name, SUM(qty) FROM sale_detail WHERE biz_date=? GROUP BY name", (d,))
                day_cost = 0
                for nr in cur2.fetchall():
                    n, q = nr
                    day_cost += costs.get(n,0)*q
                days.append({"date":d, "qty":qty, "amt":amt, "profit":amt-day_cost})
                total_cost += day_cost
            cur.execute("SELECT COALESCE(SUM(qty),0), COALESCE(SUM(amount),0) FROM sale_detail WHERE biz_date>=?", (start,))
            tq, ta = cur.fetchone()
            cur.execute("SELECT COALESCE(SUM(amount),0) FROM cash_log WHERE biz_date>=?", (start,))
            tc = cur.fetchone()[0]
            c.close()
            total_profit = ta - total_cost
            pr = (total_profit/ta*100) if ta>0 else 0
            self.send_json(200, {"start": start, "days": days, "total_qty": tq, "total_amt": ta, "total_cash": tc, "total_unpaid": ta-tc, "total_cost":total_cost, "total_profit":total_profit, "profit_rate":pr})
        else:
            self.send_json(404, {"err": "not found"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode("utf-8")
        try:
            data = json.loads(body)
        except:
            self.send_json(400, {"err": "bad json"}); return

        if self.path == "/api/prices":
            new_prices = data.get("prices", {})
            new_costs = data.get("costs", {})
            eff_date = data.get("effective_date", datetime.date.today().strftime("%Y-%m-%d"))
            c = db()
            cur = c.cursor()
            changed = 0
            current_price = get_prices()
            for name, p in new_prices.items():
                new_p = float(p)
                if abs(current_price.get(name, 0) - new_p) > 0.01:
                    cur.execute("INSERT INTO price_history(name, price_wan, effective_date, created_at) VALUES(?,?,?,?)",
                                (name, new_p, eff_date, datetime.datetime.now().strftime("%Y-%m-%d %H:%M")))
                    changed += 1
            current_cost = get_costs()
            for name, cs in new_costs.items():
                new_c = float(cs)
                if abs(current_cost.get(name, 0) - new_c) > 0.01:
                    cur.execute("INSERT INTO cost_history(name, cost_wan, effective_date, created_at) VALUES(?,?,?,?)",
                                (name, new_c, eff_date, datetime.datetime.now().strftime("%Y-%m-%d %H:%M")))
                    changed += 1
            c.commit()
            c.close()
            self.send_json(200, {"ok": True, "msg": f"已保存{changed}条调整，{eff_date}生效", "prices": get_prices(), "costs": get_costs()})

        elif self.path == "/api/scan/preview":
            # 解析完直接存到后端待处理表，刷新不丢
            text = data.get("text", "").strip()
            # 自动把操作符转大写，兼容扫码扫成小写i/o/c/e的情况
            if len(text) >= 11:
                text = text[:10] + text[10:11].upper() + text[11:]
            res = parse_qr(text)
            if "error" in res or not res.get("ok", True):
                self.send_json(400, res); return
            c = db()
            cur = c.cursor()
            cur.execute("""INSERT INTO pending_scan(raw_text, op, date, time, detail, total, preview_json, created_at)
            VALUES(?,?,?,?,?,?,?,?)""",
                        (text, res["op"], res["date"], res["time"], res.get("detail",""), res.get("total", sum(r.get("qty",0) for r in res.get("items",[]))),
                         json.dumps(res, ensure_ascii=False), datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
            pending_id = cur.lastrowid
            c.commit()
            c.close()
            self.send_json(200, {"ok": True, "pending_id": pending_id, "msg": "已存入待处理队列"})

        elif self.path == "/api/scan/commit":
            # 根据pending_id从数据库取记录，确认后入账，成功后删除待处理记录
            pending_id = data.get("pending_id", 0)
            c = db()
            cur = c.cursor()
            cur.execute("SELECT * FROM pending_scan WHERE id=?", (pending_id,))
            row = cur.fetchone()
            if not row:
                self.send_json(404, {"err": "待处理记录不存在"}); return
            res = json.loads(row["preview_json"])
            text = row["raw_text"]
            op = res["op"]
            dt = datetime.datetime.strptime(res["date"] + " " + res["time"], "%Y-%m-%d %H:%M:%S")
            bdate = biz_date(dt)
            occ = dt.strftime("%Y-%m-%d %H:%M")
            rows = res.get("items", [])
            prices = get_prices()
            costs = get_costs()
            cur.execute("INSERT INTO scan_log(biz_date,occ_time,op,raw_text,detail,total_qty,signature) VALUES(?,?,?,?,?,?,?)",
                        (bdate, occ, op, text, res.get("detail",""), res.get("total", sum(r.get("qty",0) for r in rows)), res.get("signature","")))
            scan_id = cur.lastrowid
            detail_items = []
            total_qty = 0
            total_wan = 0
            if op == "I":
                for r in rows:
                    if r["qty"] <= 0: continue
                    cur.execute("SELECT qty FROM storage WHERE name=?", (r["name"],))
                    before_qty = cur.fetchone()["qty"]
                    cur.execute("INSERT INTO in_detail(occ_time,biz_date,name,part,level,qty) VALUES(?,?,?,?,?,?)",
                                (occ, bdate, r["name"], r["part"], r["level"], r["qty"]))
                    cur.execute("UPDATE storage SET qty=qty+? WHERE name=?", (r["qty"], r["name"]))
                    after_qty = before_qty + r["qty"]
                    detail_items.append({"name":r["name"], "part":r["part"], "level":r["level"], "qty":r["qty"], "before_qty":before_qty, "after_qty":after_qty})
                    total_qty += r["qty"]
                cur.execute("SELECT COALESCE(SUM(qty),0) FROM in_detail WHERE biz_date=?", (bdate,))
                today_in_total = cur.fetchone()[0]
                cur.execute("DELETE FROM pending_scan WHERE id=?", (pending_id,))
                c.commit()
                c.close()
                self.send_json(200, {"ok": True, "scan_id": scan_id, "op": "I", "occ": occ, "items": detail_items, "total_qty": total_qty, "today_in_total": today_in_total})
            elif op == "O":
                bad = []
                for r in rows:
                    cur.execute("SELECT qty FROM storage WHERE name=?", (r["name"],))
                    row = cur.fetchone()
                    if not row or row["qty"] < r["qty"]:
                        bad.append(r["name"])
                if bad:
                    self.send_json(400, {"err": "库存不足: " + ",".join(bad)}); return
                for r in rows:
                    if r["qty"] <= 0: continue
                    cur.execute("SELECT qty FROM storage WHERE name=?", (r["name"],))
                    before_qty = cur.fetchone()["qty"]
                    unit_price = prices.get(r["name"], 0)
                    amt = unit_price * r["qty"]
                    cur.execute("INSERT INTO out_detail(occ_time,biz_date,name,part,level,qty) VALUES(?,?,?,?,?,?)",
                                (occ, bdate, r["name"], r["part"], r["level"], r["qty"]))
                    cur.execute("INSERT INTO in_detail(occ_time,biz_date,name,part,level,qty) VALUES(?,?,?,?,?,?)",
                                (occ, bdate, r["name"], r["part"], r["level"], -r["qty"]))
                    cur.execute("UPDATE storage SET qty=qty-? WHERE name=?", (r["qty"], r["name"]))
                    cur.execute("UPDATE onsale SET qty=qty+? WHERE name=?", (r["qty"], r["name"]))
                    after_qty = before_qty - r["qty"]
                    detail_items.append({"name":r["name"], "part":r["part"], "level":r["level"], "qty":r["qty"], "unit_price":unit_price, "amount":amt, "before_qty":before_qty, "after_qty":after_qty})
                    total_qty += r["qty"]
                    total_wan += amt
                cur.execute("DELETE FROM pending_scan WHERE id=?", (pending_id,))
                c.commit()
                c.close()
                self.send_json(200, {"ok": True, "scan_id": scan_id, "op": "O", "occ": occ, "items": detail_items, "total_qty": total_qty, "total_wan": total_wan})
            elif op in ("C", "E"):
                sale_qty = 0
                sale_wan = 0
                for r in rows:
                    cur.execute("SELECT qty FROM onsale WHERE name=?", (r["name"],))
                    now_row = cur.fetchone()
                    now_qty = now_row["qty"] if now_row else 0
                    delta = now_qty - r["qty"]
                    price = prices.get(r["name"], 0)
                    cost = costs.get(r["name"], 0)
                    profit_rate = (price - cost) / price if price > 0 else 0
                    profit = price * delta * profit_rate if delta > 0 else 0
                    if delta > 0:
                        amt = price * delta
                        cur.execute("INSERT INTO sale_detail(occ_time,biz_date,name,part,level,qty,unit_price,amount) VALUES(?,?,?,?,?,?,?,?)",
                                    (occ, bdate, r["name"], r["part"], r["level"], delta, price, amt))
                        sale_qty += delta
                        sale_wan += amt
                    cur.execute("UPDATE onsale SET qty=? WHERE name=?", (r["qty"], r["name"]))
                    detail_items.append({"name":r["name"], "part":r["part"], "level":r["level"], "before_qty":now_qty, "actual_qty":r["qty"], "sold_qty":max(delta,0), "unit_price":price, "amount":price*delta if delta>0 else 0, "profit_rate":round(profit_rate*100,2), "profit":round(profit) if delta>0 else 0})
                # 自动插入本轮销售汇总条
                cur.execute("INSERT INTO scan_log(biz_date,occ_time,op,raw_text,detail,total_qty) VALUES(?,?,?,?,?,?)",
                            (bdate, occ, op+"_SUMMARY", "-", f"本轮盘点销售：{sale_qty}件 / {sale_wan:.0f}万", sale_qty))
                # E票日结额外插入当日总销售汇总
                if op == "E":
                    cur.execute("SELECT COALESCE(SUM(qty),0) qty, COALESCE(SUM(amount),0) amt FROM sale_detail WHERE biz_date=?", (bdate,))
                    day_sum = cur.fetchone()
                    cur.execute("INSERT INTO scan_log(biz_date,occ_time,op,raw_text,detail,total_qty) VALUES(?,?,?,?,?,?)",
                                (bdate, occ, "E_DAY_SUMMARY", "-", f"当日总销售：{day_sum['qty']}件 / {day_sum['amt']:.0f}万", day_sum['qty']))
                cur.execute("DELETE FROM pending_scan WHERE id=?", (pending_id,))
                c.commit()
                c.close()
                op_name = "C" if op == "C" else "E"
                self.send_json(200, {"ok": True, "scan_id": scan_id, "op": op_name, "occ": occ, "items": detail_items, "sale_qty": sale_qty, "sale_wan": sale_wan})

        elif self.path == "/api/pending/delete":
            # 删除待处理记录
            pending_id = data.get("pending_id", 0)
            c = db()
            cur = c.cursor()
            cur.execute("DELETE FROM pending_scan WHERE id=?", (pending_id,))
            c.commit()
            c.close()
            self.send_json(200, {"ok": True, "msg": "已删除待处理记录"})

        elif self.path == "/api/rollback/preview":
            # 只读预检：返回撤后库存/在售前后值、是否存在后续入账票，不写库
            scan_id = data.get("scan_id", 0)
            c = db(); cur = c.cursor()
            plan = _rollback_plan(cur, scan_id)
            if "err" in plan:
                c.close(); self.send_json(plan["code"], {"err": plan["err"]}); return
            log = plan["log"]
            self.send_json(200, {"ok": True, "scan_id": scan_id, "op": plan["op"], "op_name": OP_NAMES[plan["op"]],
                "occ_time": plan["occ"], "biz_date": plan["bdate"], "detail": log["detail"],
                "has_later": plan["has_later"], "later_count": plan["later_count"], "items": plan["items"]})
            c.close()

        elif self.path == "/api/rollback":
            # 撤销 I/O 已入账票：撤前备份 → 事务内反向冲销 → 回读验算
            scan_id = data.get("scan_id", 0)
            c = db(); cur = c.cursor()
            plan = _rollback_plan(cur, scan_id)
            if "err" in plan:
                c.close(); self.send_json(plan["code"], {"err": plan["err"]}); return
            op, occ, bdate = plan["op"], plan["occ"], plan["bdate"]
            backup = _backup_db()
            try:
                cur.execute("BEGIN")
                if op == "I":
                    cur.execute("DELETE FROM in_detail WHERE occ_time=? AND biz_date=? AND qty>0", (occ, bdate))
                    for it in plan["items"]:
                        cur.execute("UPDATE storage SET qty=qty-? WHERE name=?", (it["qty"], it["name"]))
                else:
                    cur.execute("DELETE FROM out_detail WHERE occ_time=? AND biz_date=?", (occ, bdate))
                    cur.execute("DELETE FROM in_detail WHERE occ_time=? AND biz_date=? AND qty<0", (occ, bdate))
                    for it in plan["items"]:
                        cur.execute("UPDATE storage SET qty=qty+? WHERE name=?", (it["qty"], it["name"]))
                        cur.execute("UPDATE onsale SET qty=qty-? WHERE name=?", (it["qty"], it["name"]))
                cur.execute("DELETE FROM scan_log WHERE id=?", (scan_id,))
                c.commit()
            except Exception as e:
                c.rollback(); c.close(); self.send_json(500, {"err": "撤销失败已回滚: %s" % e}); return
            # 回读验算：确认无负库存/负在售
            neg = cur.execute("SELECT name,qty FROM storage WHERE qty<0").fetchall()
            neg_o = cur.execute("SELECT name,qty FROM onsale WHERE qty<0").fetchall()
            c.close()
            if neg or neg_o:
                self.send_json(500, {"err": "撤销后出现负库存/负在售，请从备份恢复: %s" % backup}); return
            self.send_json(200, {"ok": True, "msg": "已撤销 %s票 #%s" % (OP_NAMES[op], scan_id),
                "scan_id": scan_id, "op": op, "has_later": plan["has_later"],
                "items": plan["items"], "backup": backup})

        elif self.path == "/api/cash":
            amount = data.get("amount", 0)
            now = datetime.datetime.now()
            # 回款归属日期：前端可手动指定（日结后那笔回款归属当天，避免被规则记到次日）；未传或非法时回退当天
            raw_date = (data.get("biz_date") or "").strip()
            bdate = biz_date(now)
            try:
                if raw_date:
                    datetime.datetime.strptime(raw_date, "%Y-%m-%d")
                    bdate = raw_date
            except ValueError:
                bdate = biz_date(now)
            occ = now.strftime("%Y-%m-%d %H:%M")
            c = db()
            cur = c.cursor()
            cur.execute("INSERT INTO cash_log(occ_time,biz_date,amount,type,note) VALUES(?,?,?,?,?)",
                        (occ, bdate, amount, "正常", ""))
            c.commit()
            summary = today_summary(bdate)
            c.close()
            self.send_json(200, {"ok": True, "msg": f"回款{amount}万已登记（归属{bdate}）", "summary": summary})
        else:
            self.send_json(404, {"err": "not found"})

    def log_message(self, *args): pass

class ThreadingHTTPServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

if __name__ == "__main__":
    init_price()
    init_category()
    # 端口被占用先杀掉旧进程
    import socket, subprocess
    try:
        s = socket.socket()
        s.bind(("127.0.0.1", PORT))
        s.close()
    except:
        subprocess.run(["powershell", "-c", f"Get-NetTCPConnection -LocalPort {PORT} -ErrorAction SilentlyContinue | ForEach-Object {{ Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue }}"], capture_output=True)
    print(f"✅ 工作台服务已启动 http://localhost:{PORT}  [稳定多线程模式]")
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        srv.shutdown()
