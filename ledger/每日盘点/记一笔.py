"""往当天表里记一笔（盘点 / 补货 / 核对），写完自动重算汇总区与面板数据。

用法：
  python 记一笔.py 盘点 2026-11-01 00:05 "brace_60=6,ring_80=5" 期初基数
  python 记一笔.py 补货 2026-11-01 07:12 "brace_60=3"
  python 记一笔.py 核对 2026-11-01 21:05 "brace_60=4,ring_80=6" 中途盘点

简版口报（等级 + 4 个整数，按 戒、耳、镯、佩 顺序，可多级连报）：
  python 记一笔.py 盘点 2026-11-01 00:05 "60:2,5,6,4"
  python 记一笔.py 盘点 2026-11-01 00:05 "60:2,5,6,4,80:3,1,2,7"
  python 记一笔.py 补货 2026-11-01 07:12 "80:1,0,2,0"
  中文数字也可以：python 记一笔.py 盘点 2026-11-01 00:05 "60:二,五,六,四"

说明：
- 日期取营业日（0 点后的截图记当天）；时间写我登记的时刻 HH:mm。
- 补货 = 新增增量，只写这次新上架的件数；盘点 = 该时刻的存量件数；核对 = 中途看一眼的存量件数。
- 「核对」只用于算即时销量，不改在售快照；行会留在当天表里备查。
- 简版口报里每个等级必须给满 4 个数；某类没有货就写 0，顺序不能换。
"""

import csv
import json
import os
import subprocess
import sys

BASE = r"C:\Users\wangwill\Doubao\chats\2026-10-31\new-chat\梦幻西游灵饰"
DAILY_DIR = os.path.join(BASE, "每日盘点")
PRICES = os.path.join(BASE, "data", "prices.json")
SETTLE = os.path.join(DAILY_DIR, "日结.py")

HEADER = ["日期", "时间", "类型", "款号", "类型名", "等级", "名称", "数量", "执行单价(万)", "金额(万)", "备注"]
SUM_MARK = "汇总"
KIND_CN = {"ring": "戒指", "ear": "耳饰", "brace": "手镯", "pend": "佩饰"}
ORDER = ["ring", "ear", "brace", "pend"]  # 简版口报的固定顺序：戒、耳、镯、佩
CN_DIGIT = {"零": 0, "一": 1, "二": 2, "两": 2, "三": 3, "四": 4, "五": 5,
            "六": 6, "七": 7, "八": 8, "九": 9, "十": 10}


def to_int(s):
    s = s.strip()
    try:
        return int(float(s))
    except ValueError:
        pass
    if s in CN_DIGIT:
        return CN_DIGIT[s]
    if "十" in s:
        a, _, b = s.partition("十")
        tens = CN_DIGIT.get(a, 1) if a else 1
        ones = CN_DIGIT.get(b, 0) if b else 0
        return tens * 10 + ones
    raise SystemExit(f"数量格式不对：{s}")


def load_prices():
    with open(PRICES, encoding="utf-8") as f:
        return {it["code"]: it for it in json.load(f)["items"]}


def parse_items(text, prices):
    rows = []
    level = None
    qtys = []

    def flush():
        nonlocal level, qtys
        if level is None:
            return
        if len(qtys) != 4:
            raise SystemExit(f"{level}级应报 4 个数（戒、耳、镯、佩），收到 {len(qtys)} 个：{qtys}")
        for kind, q in zip(ORDER, qtys):
            code = f"{kind}_{level}"
            info = prices.get(code)
            if not info:
                raise SystemExit(f"未知款号：{code}")
            wan = (info.get("price") or 0) * q
            rows.append([code, KIND_CN[kind], str(level), info["name"], q, info.get("price"), wan])
        level, qtys = None, []

    for part in [p.strip() for p in text.replace("，", ",").replace("：", ":").split(",") if p.strip()]:
        if "=" in part:                      # 旧格式：code=数量
            flush()
            code, qty = part.split("=", 1)
            code = code.strip()
            qty = to_int(qty)
            info = prices.get(code)
            if not info:
                raise SystemExit(f"未知款号：{code}")
            lv = int(code.split("_")[1])
            wan = (info.get("price") or 0) * qty
            rows.append([code, KIND_CN.get(code.split("_")[0], ""), str(lv), info["name"], qty, info.get("price"), wan])
        elif ":" in part:                    # 简版口报：60:2 或 60:2,5,6,4 的开头
            flush()
            lv_s, q_s = part.split(":", 1)
            lv_s = lv_s.replace("级", "").strip()
            try:
                level = int(lv_s)
            except ValueError:
                raise SystemExit(f"等级格式不对：{part}")
            if q_s.strip():
                qtys.append(to_int(q_s))
        elif level is not None:              # 简版口报：当前等级的后续整数
            qtys.append(to_int(part))
        else:
            raise SystemExit(f"格式不对：{part}（应为 code=数量 或 60:2,5,6,4）")
    flush()
    return rows


def append_rows(path, new_rows, date, time, kind, note):
    body = []
    tail = []
    if os.path.exists(path):
        with open(path, encoding="utf-8-sig") as f:
            rows = list(csv.reader(f))
        idx_sum = None
        for i, r in enumerate(rows):
            if i == 0:
                continue
            if not r or not "".join(r).strip() or r[0].strip() == SUM_MARK:
                idx_sum = i
                break
            body.append(r)
        if idx_sum is not None:
            tail = rows[idx_sum:]
    else:
        body = []

    for code, kind_cn, lv, name, qty, price, wan in new_rows:
        body.append([
            date, time, kind, code, kind_cn, lv, name, str(qty),
            "" if price is None else str(price),
            "" if price is None else f"{wan:.0f}",
            note,
        ])

    out = [HEADER] + body
    if tail:
        out.append([])
        keep = [r for r in tail if r and "".join(r).strip()]
        out.extend(keep)
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        csv.writer(f).writerows(out)


def main():
    if len(sys.argv) < 5:
        raise SystemExit(__doc__)
    kind, date, hhmm, items = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    note = sys.argv[5] if len(sys.argv) > 5 else ""
    if kind not in ("盘点", "补货", "核对"):
        raise SystemExit("类型只能是 盘点 / 补货 / 核对")
    prices = load_prices()
    rows = parse_items(items, prices)
    for q in rows:
        if q[4] < 0:
            raise SystemExit("数量不能是负数")
    if kind == "补货":
        # 补货只记真上架的件数，0 件的不写表、不算事件
        rows = [q for q in rows if q[4] != 0]
        if not rows:
            raise SystemExit("补货数量全为 0，没有可记录的新上架件数")
    path = os.path.join(DAILY_DIR, f"{date}.csv")
    append_rows(path, rows, date, hhmm, kind, note)
    print(f"已记入 {os.path.basename(path)}：{kind} {hhmm} " + "，".join(f"{r[3]}×{r[4]}" for r in rows))
    subprocess.run([sys.executable, SETTLE, date], check=False)


if __name__ == "__main__":
    main()
