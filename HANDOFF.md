# FoloToy AI Passport 项目交接文档 (HANDOFF)

本文档旨在为接手本项目的开发者、维护者或 AI 助手提供完整、系统、实时的工程交接指南。涵盖硬件环境、架构设计、已实现核心功能、通讯协议、已知踩坑经验与后续演进路线。

---

## 1. 项目概览与当前演进状态

### 1.1 项目定位
本项目基于 **FoloToy AI Passport** 硬件平台（ESP32-C3，8MB Flash，无 PSRAM，240×320 ST7789 LCD 屏幕），深度定制并构建了新一代**极客便携式伴侣与 AI Token 实时监控硬件终端**。

### 1.2 当前项目状态 (截至 2026-09-13)
- **Git 最新提交**：`6e5c899` (`main` 分支已与 GitHub 同步)
- **CI 流水线**：GitHub Actions ESP-IDF v5.5.3 自动化编译全面跑通（Run ID: `34676488416`）
- **物理固件产物**：
  - `firmware/build/FoloToy-AI-Passport-full.bin` (1,206,656 字节，约 1.15MB)
  - 严格满足 3MB (`0x300000`) 出厂分区限制，无任何越界风险。
- **上位机生态**：
  - Python 3.10+ 静默后台守护服务（免 CMD 黑框、断线自愈、单实例进程锁）
  - 内置局域网 Web 个人档案配置控制台（端口 8765）

---

## 2. 硬件环境与固件运行基线

| 硬件维度 | 规格指标 / 约束 | 关键注意事项 |
| :--- | :--- | :--- |
| **主控芯片** | ESP32-C3 (单核 RISC-V 32 位) | 无外部 PSRAM，系统可用内部 RAM 仅约 150KB |
| **板载 Flash** | 8MB 物理存储 | 采用自定义分区表 `partitions.csv`，factory 分区 3MB |
| **显示屏幕** | 240 × 320 ST7789 TFT 彩屏 | SPI DMA 驱动，LVGL 采用 20 行单缓冲（约 9.6KB 静态池） |
| **背光引脚** | GPIO 21 (LEDC PWM 调光) | **严禁开启 UART0 默认 TX**（默认 TX 落在 GPIO21 会导致背光失控闪烁） |
| **通讯与调试**| 原生 USB-Serial-JTAG (GPIO 18/19) | 控制台走 CDC 驱动通道，支持 FAP 串口截屏协议 |
| **出厂恢复** | GPIO 0 (UP 按键) | 长按 UP 键 5 秒由 Bootloader 直接拉起 0x700000 恢复镜像 |

---

## 3. 核心功能全景与交互规范

### 3.1 系统大菜单启动器 (Launcher)
开机后默认进入高质感卡片式系统大菜单，提供 5 大模块自由切换：
1. **AI Token 监控器**：主力模型剩余配额、今日 Token 总用量、实时预估花费、任务并发数；
2. **个人智能主页**：极客赛博工牌、像素宠物伴侣立绘、认证编号、双击弹二维码扫码配置；
3. **内置游戏中心**：贪吃蛇（Snake）与跳跳恐龙（Dino Run），动态离线娱乐；
4. **系统设置菜单**：屏幕亮度（5 档调节）、宠物形象切换（18 种伴侣）、恢复出厂；
5. **设备关于页面**：固件版本、MAC 识别码、BLE 状态与电池实时电压采样。

> **全局人机工学交互**：
> - 在任何二级页面、游戏或设置中，**长按 OK 键 (>800ms) 无条件平滑返回系统大菜单**。

---

### 3.2 极低功耗待机与智能节能系统 (重点特性)
针对前期设备发热、耗电快的问题，系统构建了**两段式智能节能定时器**与底层调度深度休眠：
- **开机背光优化**：默认背光由 100% 调谐为护眼透亮的 **60%**（背光功耗直接降低 50%）；
- **30 秒空闲调暗**：无操作 30 秒自动平滑调暗至 **20% 微光**（节能同时提示用户）；
- **60 秒空闲息屏**：无操作 60 秒自动关闭背光进入**深层息屏节能待机**；
- **FreeRTOS 调度节流**：
  - 息屏状态下 FreeRTOS 主任务挂起等待时长由 100ms 延长至 **1000ms**；
  - 息屏状态下**彻底跳过 `buddy_render()`** 与 SPI DMA 传输，杜绝空转刷屏；
  - 启用 FreeRTOS Tickless Idle (`CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`)；
  - 启用 NimBLE Modem Sleep (`CONFIG_BT_NIMBLE_SLEEP_ENABLE=y`)；
  - 广播间隔调优为 100ms~200ms，射频待机电流降低 70%；
  - 整机待机功耗从 ~100mA 骤降至 **< 10mA**（电池续航提升 5~10 倍）。
- **零延迟唤醒**：
  - 短按任意按键（UP / DOWN / OK）：屏幕在 **10ms 内瞬间点亮**恢复设定亮度（首次唤醒按键不误触发界面动作）；
  - 收到蓝牙数据推送（Token 更新、扫码改名、权限弹窗）：屏幕自动点亮；
  - 玩游戏期间智能续期活跃计时，绝不在操作过程中误熄屏。

---

### 3.3 个人主页与扫码修改系统 (QR Web Console)
- **副屏弹窗**：在个人智能主页快速**双击 OK 键**，调用轻量 `qrcode.c` 引擎（仅占用 200 字节栈内存）瞬间渲染 29×29 矩阵黑白二维码；
- **极速修改**：手机或电脑扫描二维码访问 `http://<局域网IP>:8765/profile`；
- **双向实时同步**：Web 页面点击保存后，后台通过 BLE 协议毫秒级下发 `name` 与 `owner` 指令，单片机写入内部 NVS Flash，即刻重绘且断电重启不丢失。

---

## 4. 工程目录与源码导航

```text
Ai passport/
├── firmware/                       # ESP32-C3 固件源码 (ESP-IDF 5.5.3)
│   ├── CMakeLists.txt              # 固件 CMake 入口
│   ├── partitions.csv              # 3MB factory 应用分区表
│   ├── sdkconfig.defaults          # 尺寸优化 (-Os)、Tickless Idle、NimBLE 睡眠配置
│   ├── bootloader_components/      # 5 秒长按恢复出厂 Bootloader 扩展
│   ├── components/bsp/             # 板级外设驱动 (Display, Battery, Buttons, I2C, LVGL)
│   └── main/                       # 固件主业务逻辑
│       ├── main.c                  # 系统启动入口、FreeRTOS 主任务、低功耗队列挂起
│       ├── buddy_state.c / .h      # 核心状态机 (大菜单、主页、30s/60s 节能定时器、按键处理)
│       ├── buddy_ui.c / .h         # LVGL 界面绘制 (大菜单、名片工牌、Token 面板、弹窗)
│       ├── buddy_protocol.c / .h   # BLE NUS JSON 协议解析器 (具备多字段回退兼容)
│       ├── buddy_ble.c / .h        # NimBLE GATT/GAP 服务层 (配置 100-200ms 广播间隔)
│       ├── buddy_games.c / .h      # 贪吃蛇与恐龙跳跃小游戏状态机与渲染
│       ├── qrcode.c / .h           # 零动态内存开销的嵌入式 QR Code 生成器
│       ├── buddy_font_zh_14/16.c   # 项目专属精简高频汉字与 ASCII 点阵字库
│       └── buddy_types.h           # 核心事件、动作与数据结构定义
├── tools/                          # 上位机 Python 守护工具与测试套件
│   ├── silent_bridge.py            # AI Passport 静默后台守护服务 (免 CMD 窗口)
│   ├── token_monitor_bridge.py     # BLE NUS 通讯与 Web 扫码配置服务 (8765 端口)
│   ├── token_monitor_collector.py  # 35+ 种 AI 编程工具 Token 用量与配额采集器
│   ├── flash_firmware.py           # 智能端口识别与即插即用等待烧录脚本
│   └── profile_config.json         # 个人档案本地持久化文件
├── .learnings/                     # 架构经验沉淀与防御规则
│   └── embedded_ui_and_qr.md       # C3 内存、二维码与 DMA 防御规范
├── 一键烧录全新固件.bat              # Windows 一键全自动烧录批处理 (UTF-8 编码)
├── 启动后台监控.bat                  # 一键静默启动守护服务
├── 停止后台监控.bat                  # 一键停止后台守护服务
├── 查看实时日志.bat                  # 实时输出 logs/passport_service.log
├── README.zh_CN.md                 # 项目详细中文介绍
└── HANDOFF.md                      # 本交接文档
```

---

## 5. 通信协议与数据契约

### 5.1 蓝牙 BLE NUS (Nordic UART Service) 数据格式
设备开启 NUS 服务（Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`，RX 特征值: `6E400002...`，TX 特征值: `6E400003...`）。

#### 1) Token 监控心跳包 (每 10 秒下发)
```json
{
  "total": 1,
  "running": 1,
  "waiting": 0,
  "tokens": 123500000,
  "tokens_today": 123500000,
  "message": "主力: Gemini 3.8 Flash 剩余 21% | 今日 Token: 123.5M | 预估: ¥48.68 | 任务: 1",
  "entries": ["Gemini 3.8 Flash (21%)", "Claude 3.7 Sonnet (85%)"]
}
```

#### 2) 个人档案修改包 (双向全字段容错)
为防止协议版本不匹配导致的指令丢弃，固件与上位机统一采用多重回退格式：
- **修改昵称**：
  ```json
  {"cmd": "name", "name": "syhx", "value": "syhx"}
  ```
- **修改账号/邮箱**：
  ```json
  {"cmd": "owner", "name": "syhx.ikun@gmail.com", "owner": "syhx.ikun@gmail.com", "value": "syhx.ikun@gmail.com"}
  ```

---

## 6. 关键踩坑经验与永久防御机制 (.learnings)

接手者在后续迭代中**必须严格遵守**以下准则，防止已知 Bug 重蹈覆辙：

1. **Windows 串口 DTR/RTS 复位陷阱**：
   - 在 Windows 环境下通过 `pyserial` 打开 ESP32-C3 串口时，系统驱动会默认脉冲拉低 DTR/RTS 引脚触发单片机硬件复位，导致蓝牙频繁掉线。
   - **防御准则**：任何上位机串口连接代码必须显式声明：
     ```python
     s = serial.Serial()
     s.port = port
     s.dtr = False
     s.rts = False
     s.open()
     ```

2. **ESP32-C3 内存与 DMA 碎片防御**：
   - C3 芯片完全没有外部 PSRAM，内部连续大块内存极度匮乏。
   - **防御准则**：
     - **严禁**在运行时动态 `malloc` 超过 10KB 的连续内存；
     - LVGL DMA 缓冲严格限制在 20 行单缓冲（约 9.6KB）；
     - 二维码使用栈分配的 `qrcode.c`，严禁引入需巨大内存的臃肿二维码库；
     - 串口截屏协议缓冲区必须在编译期静态预留（64字节对齐）。

3. **固件编译与链接器 Wrap 防御**：
   - 固件在 `firmware/main/CMakeLists.txt` 中通过 `-Wl,--wrap=ble_att_svr_register` 注入了 BLE CCCD 权限加固逻辑。
   - **防御准则**：
     - `sdkconfig.defaults` 保持 `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`（`-Os` 优化）；
     - **切勿轻易开启 `CONFIG_COMPILER_OPTIMIZATION_LTO=y`**，GCC 链接器在全程序优化时可能会内联或裁减符号，导致 wrap 函数失效。

4. **批处理文件 Windows 控制台编码**：
   - Windows cmd.exe 默认代码页为 GBK（CP936），若直接使用无 BOM 的 UTF-8 中文批处理文件，控制台会报语法解析错误与乱码。
   - **防御准则**：所有 `.bat` 批处理文件顶部必须加入 `@chcp 65001 >nul`，并在输出文本中保持标准转义。

---

## 7. 日常运维、烧录与验证指南

### 7.1 固件烧录 (零摩擦即插即用)
1. 用 USB Type-C 数据线将开发板连接至电脑；
2. 双击运行根目录下的 **`一键烧录全新固件.bat`**；
3. 脚本内置自动轮询机制，未检测到设备时会静默等待插入，插入后自动在 5 秒内完成 100% 刷入并自动复位重启。

### 7.2 上位机后台监控守护服务
- **启动服务**：双击 **`启动后台监控.bat`**（将在后台静默运行 `silent_bridge.py`，无黑框干扰）；
- **停止服务**：双击 **`停止后台监控.bat`**（安全杀掉当前运行中的桥接进程与 PID 文件）；
- **查看实时日志**：双击 **`查看实时日志.bat`**（实时输出 `logs/passport_service.log`）；
- **开机自启动**：双击 **`设置开机自动启动.bat`**（向 Windows 当前用户 Startup 写入静默 VBS 脚本）。

---

## 8. 后续演进建议 (Roadmap)

1. **更多极客小游戏扩展**：当前架构已实现游戏循环框架 `buddy_games.c`，可继续扩展 2048、俄罗斯方块等离线小游戏；
2. **多设备配对管理**：当前 NimBLE 配置限制连接数为 1，可增加已配对上位机列表在系统设置中快速切换；
3. **电量精确电量计算法**：目前走简单的 ADC 电压阶梯映射（3.2V~4.15V），后续可引入简易开路电压（OCV）平滑滤波算法，使电量百分比更加线性稳定。
