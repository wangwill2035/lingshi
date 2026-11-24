import subprocess, json, sqlite3, os

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mhxy.db")
TOKEN = "OaB5s9oOVhp9VXtH8bWc1csNnlf"

SHEETS = {
    "scan": "UwurIy", "in": "1eXNsG", "out": "2uGnKI",
    "sale": "3GGzTh", "cash": "0WFd8u", "onsale": "QIPiHK", "storage": "aYEpNs"
}

ITEMS = [
    ("枫华戒","戒指","60级",342),("翠叶环","耳饰","60级",342),
    ("香木镯","手镯","60级",157),("芝兰佩","佩饰","60级",157),
    ("芙蓉戒","戒指","80级",762),("明月珰","耳饰","80级",742),
    ("翡玉镯","手镯","80级",387),("逸云佩","佩饰","80级",387),
    ("金麟绕","戒指","100级",1357),("玉蝶翩","耳饰","100级",1317),
    ("墨影扣","手镯","100级",517),("莲音玦","佩饰","100级",517),
    ("悦碧水","戒指","120级",2315),("点星芒","耳饰","120级",2165),
    ("花映月","手镯","120级",845),("相思染","佩饰","120级",885),
]

def lark_range(sheet_id, start_row, end_row, col_letters):
    rng = f"A{start_row}:{col_letters}{end_row}"
    try:
        r = subprocess.run(["lark-cli","sheets","+cells-get","--spreadsheet-token",TOKEN,
                            "--sheet-id",sheet_id,"--range",rng,
                            "--format","json"],
                           capture_output=True, text=True, encoding="utf-8", timeout=30)
        out = r.stdout
        start = out.find("{")
        if start < 0: return []
        data = json.loads(out[start:])
        # 如果输出写到文件了，读文件（文件顶层直接是ranges结构）
        if "output_path" in data.get("data", {}):
            with open(data["data"]["output_path"], "r", encoding="utf-8") as f:
                filedata = json.load(f)
            return filedata["ranges"][0]["cells"]
        if "ranges" not in data.get("data", {}):
            return []
        return data["data"]["ranges"][0]["cells"]
    except Exception as e:
        print(f"  skip {rng}: {e}")
        return []

def v(cell, i):
    try:
        x = cell[i].get("value")
        return x if x is not None else ""
    except: return ""

def read_all(sheet_id, max_rows, col_letters):
    rows = []
    step = 30
    for s in range(2, max_rows, step):
        e = min(s+step-1, max_rows)
        cells = lark_range(sheet_id, s, e, col_letters)
        for row in cells:
            if v(row,0): rows.append(row)
        if not cells: break
    return rows

def main():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    for t in ["scan_log","in_detail","out_detail","sale_detail","cash_log"]:
        c.execute(f"DELETE FROM {t}")

    # 1. 扫码日志
    print("导入扫码日志...")
    rows = read_all(SHEETS["scan"], 200, "F")
    for row in rows:
        c.execute("INSERT INTO scan_log(biz_date,occ_time,op,raw_text,detail,total_qty) VALUES(?,?,?,?,?,?)",
                  (v(row,0), v(row,1), v(row,2), v(row,3), v(row,4), v(row,5)))
    print(f"  {len(rows)}行")

    # 2. 入库明细
    print("导入入库明细...")
    rows = read_all(SHEETS["in"], 500, "F")
    for row in rows:
        c.execute("INSERT INTO in_detail(occ_time,biz_date,name,part,level,qty) VALUES(?,?,?,?,?,?)",
                  (v(row,0), v(row,1), v(row,2), v(row,3), v(row,4), v(row,5)))
    print(f"  {len(rows)}行")

    # 3. 上架明细
    print("导入上架明细...")
    rows = read_all(SHEETS["out"], 500, "F")
    for row in rows:
        c.execute("INSERT INTO out_detail(occ_time,biz_date,name,part,level,qty) VALUES(?,?,?,?,?,?)",
                  (v(row,0), v(row,1), v(row,2), v(row,3), v(row,4), v(row,5)))
    print(f"  {len(rows)}行")

    # 4. 销售明细
    print("导入销售明细...")
    rows = read_all(SHEETS["sale"], 500, "J")
    for row in rows:
        c.execute("INSERT INTO sale_detail(occ_time,biz_date,name,part,level,qty,unit_price,amount,profit_rate,profit) VALUES(?,?,?,?,?,?,?,?,?,?)",
                  (v(row,0), v(row,1), v(row,2), v(row,3), v(row,4), v(row,5),
                   float(v(row,6) or 0), float(v(row,7) or 0), v(row,8), float(v(row,9) or 0)))
    print(f"  {len(rows)}行")

    # 5. 回款
    print("导入回款记录...")
    rows = read_all(SHEETS["cash"], 200, "E")
    for row in rows:
        c.execute("INSERT INTO cash_log(occ_time,biz_date,amount,type,note) VALUES(?,?,?,?,?)",
                  (v(row,0), v(row,1), float(v(row,2) or 0), v(row,3) or "正常", v(row,4)))
    print(f"  {len(rows)}行")

    conn.commit()
    print("\n=== 导入完成 ===")
    for t in ["scan_log","in_detail","out_detail","sale_detail","cash_log","onsale","storage"]:
        c.execute(f"SELECT COUNT(*) FROM {t}")
        print(f"  {t}: {c.fetchone()[0]}行")
    conn.close()

if __name__ == "__main__":
    main()
