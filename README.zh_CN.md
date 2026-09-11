# FoloToy AI Passport · Token Monitor 硬件用量伙伴

**简体中文** · [English](README.md)

本工程将 [Javis603/token-monitor](https://github.com/Javis603/token-monitor) 的多 AI 工具 Token 用量、费用估算与配额监控能力深度移植至 [FoloToy AI Passport](https://github.com/folotoy/ai-passport) 硬件。

设备端采用 ESP32-C3 架构，**应用固件体积严格控制在 3MB（0x300000 字节）受保护的物理分区上限以内**。宿主端提供免安装 Electron 的轻量化采集引擎，支持实时追踪 **Antigravity、Claude Code、Codex、Cursor** 等 35+ 种 AI 编程工具的实时 Token 与配额，并通过 Nordic UART Service (NUS) BLE 无线流式同步至屏幕。

---

## 核心特性

- **硬件体积严格受控 (< 3MB)**：
  - 针对 ESP32-C3 8MB Flash 无 PSRAM 硬件基线设计；
  - 严格契约 `factory, app, factory, 0x10000, 0x300000`（3MB 上限）与 `cardid@0x356000`；
  - 采用 `-Os` 尺寸优化、LTO 与轻量像素中文字库，源码与资源静态占用 < 800 KB。
- **三重视图界面 (支持按键切换)**：
  - **页面 1：Token 监控主屏 (Home)**：大字呈现今日 Token 用量（`128.5 K` / `1.2 M`）、今日预估开销（`$1.42` / `¥9.80`）、活跃任务数、电池电量与状态指示灯。
  - **页面 2：多工具配额中心 (Limits)**：双卡片展示主力工具（Antigravity / Gemini / Codex 5小时窗口 / Claude 7天窗口）的 10 格点阵进度条与到期重置倒计时。
  - **页面 3：AI 工具消耗明细 (Tools)**：当日排名前列的各 AI 编程工具（Claude Code, Codex, Antigravity, Cursor 等）Token 消耗与费用排行榜。
  - **页面 4 & 5**：宠物伴侣动画视图与设备关于信息。
- **任务状态联动与动效**：
  - 本地实时感知 Agent 正在工作 / 空闲状态；
  - 任务生成完成时，屏幕触发 6 秒“任务已完成”庆祝动效。

---

## 目录结构

```text
├── firmware/                       # ESP32-C3 嵌入式固件 (<= 3MB)
│   ├── CMakeLists.txt              # CMake 构建配置
│   ├── partitions.csv              # 3MB factory 应用分区表
│   ├── sdkconfig.defaults          # -Os 尺寸优化与 NimBLE 配置
│   ├── bootloader_components/      # 5秒长按恢复出厂 Bootloader
│   ├── components/                 # 板级 BSP 与驱动
│   └── main/                       # 核心业务状态机与 Token Monitor UI
│       ├── buddy_ui.c              # Token Monitor 主看版与多页面渲染
│       ├── buddy_protocol.c        # 增强型 BLE JSON 协议解析
│       └── buddy_types.h           # 数据结构定义
├── tools/                          # 宿主机轻量采集与桥接器 (< 2MB)
│   ├── token_monitor_collector.py  # 35+ AI 工具本地用量聚合器
│   ├── token_monitor_bridge.py     # BLE 蓝牙硬件同步桥接器
│   ├── check_firmware_size.py      # 固件 3MB 分区安全审计门禁脚本
│   └── requirements.txt            # Python 依赖
├── tests/                          # 跨平台全套自动化单元测试
├── README.zh_CN.md                 # 中文说明文档
└── README.md                       # 英文说明文档
```

---

## 编译与固件烧录

要求激活 ESP-IDF 5.5.3 环境，目标芯片为 ESP32-C3：

```bash
# 1. 激活 ESP-IDF 环境
get_idf553

# 2. 进入固件目录并设置目标芯片
cd firmware
idf.py set-target esp32c3

# 3. 编译工程
idf.py build

# 4. 执行 3MB 固件体积审计（门禁检查）
python ../tools/check_firmware_size.py

# 5. 烧录至 AI Passport 并打开监控
idf.py flash monitor
```

> **配对提示**：首次连接时，AI Passport 屏幕将显示 6 位安全配对码，在电脑系统弹出的蓝牙配对框中输入即可绑定。

---

## 运行宿主机桥接器

宿主机桥接器负责自动扫描宿主机上的 AI 工具日志、会话和后台 RPC 服务，汇总后推送到设备：

```bash
# 1. 安装依赖
pip install -r tools/requirements.txt

# 2. 离线验证模式 (无需蓝牙硬件，在终端模拟屏幕显示与报文)
python tools/token_monitor_bridge.py --dry-run

# 3. 蓝牙实时同步模式
python tools/token_monitor_bridge.py

# 4. 周围有多台设备时指定设备名称或 MAC 地址
python tools/token_monitor_bridge.py --device Codex-A1B2C3
```

---

## 硬件按键操作指南

- **UP 键**：按序循环切换视图：
  - `首页看板` (今日 Token 与总开销) → `配额中心` (双窗口配额与倒计时) → `工具明细` (各工具消耗排行) → `宠物伴侣` → `设备信息`
- **DOWN 键**：在工具明细页或信息页向下滚动或切换子页。
- **长按 OK 键**：打开系统设置菜单（屏幕亮度、解绑蓝牙、出厂重置等）。

---

## 自动化测试验证

本工程提供完整的宿主机测试用例：

```bash
# 执行 Python 协议与采集器测试
python -m unittest discover -s tests

# 执行固件大小上限审计
python tools/check_firmware_size.py
```
