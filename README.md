# lingshi · 梦幻西游灵饰经营管理工具集

本仓库收录「灵饰台账系统」与「入库/上架二维码打印工具」的程序代码与规范文档，用于版本留档、协作开发与经验沉淀。

## 目录结构

```
lingshi/
├── README.md
├── docs/
│   ├── 操作规范.md              # 标准作业规范（SOP，日常执行以此为准）
│   ├── 工作流程指导.md          # 完整流程与历次复盘固化条目
│   ├── 工作台开发技术文档.md
│   ├── 二维码工具-技术手册.md
│   ├── 二维码工具-开发回顾.md
│   ├── 体活产出核算.md
│   ├── 桌面应用化技术论证.md
│   └── 工作流程思维导图.html
├── ledger/                      # 台账系统
│   ├── app_server.py            # 服务端（HTTP + SQLite + 扫码解析，打包源码）
│   ├── server.py                # 历史回退快照，不被启动入口调用
│   ├── 工作台.html              # 前端单页工作台
│   ├── 灵饰价格记忆库.html
│   ├── db_init.py / db_import.py
│   ├── gen_daily_report.py / build_page.py / gen_bat.py
│   ├── check_prices.py / update_prices.py / fix_prices.py
│   ├── 库存.py                  # 库存小工具
│   ├── 每日盘点/                # 记一笔 / 期初表 / 日结 / 月报
│   ├── 汇率悬浮窗/              # 汇率悬浮窗脚本与使用说明
│   ├── 管家截图.ps1 / 现金巡检.ps1
│   ├── 启动工作台-exe.bat
│   ├── win32/                   # 原生 C++ 工作台（main.cpp）与数据库核查脚本
│   └── data/                    # 品类元数据与价格快照
└── qrtool/                      # 入库/上架二维码打印工具（纯 Win32 C++）
    ├── src/main.cpp             # 主程序
    ├── src/print_page.hpp       # 打印页面布局
    ├── src/app.rc / app.manifest / app.ico
    ├── build.ps1
    └── third_party/qrcodegen/   # 二维码生成库
```

## 快速上手

### 台账系统

1. 安装 Python 3.x，准备 SQLite 库（`mhxy.db` 不入本仓库，含经营数据）。
2. `python db_init.py` 初始化表结构，`python db_import.py` 导入基础数据。
3. 双击 `启动工作台-exe.bat`（或 `python app_server.py`）启动服务，浏览器自动打开工作台。
4. 扫码按 `docs/操作规范.md` 第四节的 I/O/C/E 契约执行。

### 二维码打印工具

1. 依赖 MinGW-w64（w64devkit）+ 自带 `third_party/qrcodegen`。
2. 执行 `build.ps1` 编译，产出单 exe；界面资源为 `src/背景图片.png`、`src/top.png`、`src/app.ico`。
3. 打印版式见 `src/print_page.hpp` 与 `docs/二维码工具-技术手册.md`。

## 关键约定

- **业务日以 E 票日结为唯一分界**，不按 0 点或 22 点自动跳天。
- **库存与在售双轨独立维护**：入库只加库存，上架库存减、在售加，销售由盘差反推。
- **价格以数据库 `price_history` / `cost_history` 最新记录为准**，仓库内价格文件仅为历史快照。
- **撤票、盘库校正、勘误前必须先备份数据库**，并在事务内执行、回读验算。

## 未纳入仓库内容

数据库文件（`mhxy.db` 及各类 `.bak`）、打包产物（`*.exe`、`dist/`、`build/`）、每日盘点 CSV/XLSX 经营数据、飞书 API 抓取缓存（`sheets/`）、编译工具链与调试截图，均不纳入本仓库，见 `.gitignore`。

## 文档

日常执行请直接看 [docs/操作规范.md](docs/操作规范.md)；完整历史复盘条目见 [docs/工作流程指导.md](docs/工作流程指导.md)。
