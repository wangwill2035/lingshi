# -*- coding: utf-8 -*-
"""把 data/prices.json + icons/ 合成一个自包含的 HTML 灵饰价格记忆库（图标内嵌 base64）。"""
import base64
import json
import os

BASE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(BASE, "data", "prices.json")
ICON_DIR = os.path.join(BASE, "icons")
OUT = os.path.join(BASE, "灵饰价格记忆库.html")

LEVELS = [60, 80, 100, 120, 140, 160]
TYPES = ["戒指", "耳饰", "手镯", "佩饰"]

CSS = """
*{box-sizing:border-box}
body{margin:0;padding:24px 20px 40px;background:#0d0f14;color:#e6e9ef;
 font-family:"Microsoft YaHei","PingFang SC",system-ui,-apple-system,"Segoe UI",sans-serif}
.wrap{max-width:1180px;margin:0 auto}
h1{margin:0;font-size:26px;letter-spacing:1px;color:#f2e2c4}
.sub{margin:8px 0 0;font-size:14px;color:#8b93a3}
.chips{display:flex;flex-wrap:wrap;gap:10px;margin:18px 0 6px}
.chip{padding:6px 12px;border:1px solid #2b3140;border-radius:999px;background:#141821;
 font-size:13px;color:#b9c1d0}
.chip b{color:#e8c37a;font-weight:600}
.gridwrap{margin-top:18px;overflow-x:auto;padding-bottom:4px}
table{border-collapse:separate;border-spacing:10px;min-width:900px}
th{font-size:13px;font-weight:500;color:#8b93a3;padding:0 0 2px;text-align:center}
th.lv{color:#cfd6e4;font-size:14px}
td.type{background:transparent;vertical-align:middle;width:64px}
.tname{writing-mode:vertical-rl;letter-spacing:6px;font-size:15px;color:#e8c37a;
 border-left:3px solid #3d3520;padding-left:10px;line-height:1}
.cell{width:100%;min-width:132px;background:#151a23;border:1px solid #262c39;border-radius:12px;
 padding:12px 10px 10px;text-align:center;vertical-align:top}
.cell .ic{width:56px;height:56px;border-radius:10px;object-fit:cover;background:#0a0c10;
 border:1px solid #2b3140;display:block;margin:0 auto 8px}
.cell .nm{font-size:15px;color:#e9edf5;letter-spacing:.5px}
.cell .price{font-size:21px;font-weight:700;color:#f2c96b;margin-top:6px;letter-spacing:.5px}
.cell .price.zero{font-size:15px;font-weight:400;color:#59616f;letter-spacing:0}
.cell .rmb{font-size:11.5px;color:#8f98ab;margin-top:1px}
.cell .meta{font-size:11px;color:#6f7787;margin-top:2px}
.cell.dim{opacity:.55}
.cell.dim .nm{color:#aab2c1}
.legend{margin-top:22px;font-size:12.5px;line-height:2.0;color:#7d859a}
.legend b{color:#aeb6c6;font-weight:600}
"""


def icon_uri(code):
    for ext, mime in (("jpg", "image/jpeg"), ("jpeg", "image/jpeg"), ("png", "image/png")):
        p = os.path.join(ICON_DIR, "%s.%s" % (code, ext))
        if os.path.exists(p):
            with open(p, "rb") as f:
                b = base64.b64encode(f.read()).decode("ascii")
            return "data:%s;base64,%s" % (mime, b)
    return ""


def main():
    with open(DATA, encoding="utf-8") as f:
        book = json.load(f)
    items = book["items"]
    idx = {(i["type"], i["level"]): i for i in items}
    priced = [i for i in items if i.get("price") is not None]
    hi_p = max(i["price"] for i in priced)
    lo_p = min(i["price"] for i in priced)
    total = sum(i["price"] for i in priced)
    rate_lo, rate_hi = 0.070, 0.100  # 元/万梦幻币：时间服下限~畅玩服上限（2026-10-30~31 公开快照）
    rmb_range = "¥%d–%d" % (round(total * rate_lo), round(total * rate_hi))
    # 本服金价：白云山（广东4区）¥224.01 / 3000万，采集 2026-10-31 10:40（阿鱼工具箱）
    SERVER = "白云山（广东4区）"
    RATE_Y3000 = 224.01
    RATE = RATE_Y3000 / 3000.0
    rmb_server = "{:,}".format(round(total * RATE))
    hi = [i["name"] for i in priced if i["price"] == hi_p]
    lo = [i["name"] for i in priced if i["price"] == lo_p]
    missing_140 = [i["name"] for i in items if i["level"] == 140 and i.get("price") is None]
    if missing_140:
        pending_note = ("140 级四款（%s）暂无报价，标为“待报价”；若你表格底部被裁掉，请补一张完整表。"
                        % "、".join(missing_140))
    else:
        lv140 = [i for i in items if i["level"] == 140]
        pending_note = ("140 级四款已补录（%s，%s 录入）；160 级四款仅作图标备查，未报价。"
                        % (" / ".join("%s %s" % (i["name"], i["price"]) for i in lv140), book["updated_at"][:10]))

    head = "".join("<th class=\"{}\">{}</th>".format("lv", "%d级" % lv) if lv != 160 else
                   "<th class=\"lv\" style=\"opacity:.6\">160级</th>" for lv in LEVELS)

    rows = []
    for t in TYPES:
        cells = []
        for lv in LEVELS:
            it = idx[(t, lv)]
            price = it.get("price")
            dim = " dim" if lv == 160 else ""
            if price is None:
                price_html = '<div class="price zero">待报价</div>'
                meta = "未在你的价格表内" if lv != 160 else "备查"
            else:
                price_html = ('<div class="price">%s</div><div class="rmb">≈¥%d</div>'
                              % (price, round(price * RATE)))
                meta = "在售"
            cells.append(
                '<td><div class="cell%s">'
                '<img class="ic" src="%s" alt="%s">'
                '<div class="nm">%s</div>%s<div class="meta">%s</div>'
                '</div></td>' % (dim, icon_uri(it["code"]), it["name"], it["name"], price_html, meta)
            )
        rows.append('<tr><td class="type"><div class="tname">%s</div></td>%s</tr>' % (t, "".join(cells)))

    html = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>灵饰价格记忆库</title>
<style>%s</style>
</head>
<body>
<div class="wrap">
  <h1>灵饰价格记忆库</h1>
  <p class="sub">60–160 级 · 戒指 / 耳饰 / 手镯 / 佩饰 · 名称与图标同步《梦幻西游》官网《灵饰系统》资料页 · 单价单位：万梦幻币</p>
  <div class="chips">
    <div class="chip">更新时间 <b>%s</b></div>
    <div class="chip">已录价格 <b>%d</b> 款</div>
    <div class="chip">最高 <b>%s %s</b></div>
    <div class="chip">最低 <b>%s %s</b></div>    <div class="chip">待报价 <b>%d</b> 款</div>
    <div class="chip">合计 <b>%s</b> 万梦幻币</div>
    <div class="chip">按白云山金价 ≈ <b>¥%s</b></div>
  </div>
  <div class="gridwrap">
    <table>
      <tr><th></th>%s</tr>
      %s
    </table>
  </div>
  <div class="legend">
    <b>口径</b>：单价单位为<b>万梦幻币</b>（1 万梦幻币 = 10,000 两），按你表格原样记录；160 级不在本次在售清单内，仅作图标备查。<br>
    <b>汇率</b>：梦幻币没有官方汇率，每个区各自定价，看该服的藏宝阁与公开金价。本表按你所在的 <b>白云山（广东4区）</b> 金价 ¥224.01 / 3000 万折算（1 元 ≈ 13.39 万，1 万 ≈ ¥%.4f，采集 2026-10-31 10:40），20 款合计 %s 万 ≈ <b>¥%s</b>。区间参考：时间服 1 万 ≈ ¥0.070–0.079、畅玩服 ≈ ¥0.090–0.100；全站 155 服均价 ¥209.73 / 3000 万。金价每天波动，实际到手以成交价与手续费为准。<br>
    <b>待补</b>：%s<br>
    <b>图标来源</b>：官网《灵饰系统》资料页原图；莲音玦因该页图源失效，取自游戏内截图裁切，与其余图标底色略有差异。<br>
    <b>盘点用法</b>：发摆摊/背包截图给我，我按图标逐格比对 → 输出各款数量合计 + 本次记录时间，并写入 data/prices.json 的 count_log。
  </div>
</div>
</body>
</html>
""" % (CSS, book["updated_at"], len(priced),
       hi_p, "／".join(hi), lo_p, "／".join(lo),
       len(items) - len(priced), "{:,}".format(total), rmb_server,
       head, "".join(rows), RATE, "{:,}".format(total), rmb_server, pending_note)

    with open(OUT, "w", encoding="utf-8") as f:
        f.write(html)
    print("written: %s" % OUT)
    print("items=%d priced=%d imgs=%d size_kb=%d" % (
        len(items), len(priced), html.count('<img class="ic"'), len(html.encode("utf-8")) // 1024))


if __name__ == "__main__":
    main()
