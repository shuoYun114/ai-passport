# FoloToy AI Passport · Token Monitor Hardware Companion

[简体中文](README.zh_CN.md) · **English**

This project ports the multi-tool token tracking, cost estimation, and quota monitoring capabilities of [Javis603/token-monitor](https://github.com/Javis603/token-monitor) to the [FoloToy AI Passport](https://github.com/folotoy/ai-passport) hardware.

Designed for the ESP32-C3 architecture, **the application firmware size is strictly constrained within the 3 MB (0x300000 bytes) protected physical partition limit**. The host bridge provides an Electron-free lightweight collector engine supporting real-time tracking across 35+ AI coding tools—including **Antigravity, Claude Code, Codex, and Cursor**—streaming metrics to the AI Passport display over Nordic UART Service (NUS) BLE.

---

## Key Features

- **Strict Size Constraint (< 3MB)**:
  - Tailored for ESP32-C3, 8 MB Flash, no PSRAM hardware baseline.
  - Mandatory protected partition contract: `factory, app, factory, 0x10000, 0x300000` (3MB max) and `cardid@0x356000`.
  - Built with `-Os` size optimization, link-time optimization (LTO), and trimmed pixel Chinese fonts.
- **Triple Screen Views (Toggled via UP Button)**:
  - **Screen 1: Token Monitor Dashboard (Home)**: High-contrast display of today's tokens (`128.5 K` / `1.2 M`), estimated costs (`$1.42`), active task counts, and battery indicator.
  - **Screen 2: Multi-Tool Limits Center**: Dual-slot 10-segment dot-matrix progress bars and reset countdowns for primary/secondary tools (Antigravity/Gemini/Codex/Claude).
  - **Screen 3: AI Tools Breakdown**: Real-time ranking of daily token usage and cost per tool (Claude Code, Codex, Antigravity, Cursor, etc.).
  - **Screens 4 & 5**: Desktop virtual companion animation and device information.
- **Task Status & Celebration**:
  - Automatically identifies whether an AI agent is active or idle.
  - Triggers a 6-second celebratory animation upon task completion.

---

## Building and Flashing Firmware

Requires ESP-IDF 5.5.3 targeted at ESP32-C3:

```bash
# 1. Activate ESP-IDF
get_idf553

# 2. Navigate and set target
cd firmware
idf.py set-target esp32c3

# 3. Build firmware
idf.py build

# 4. Audit binary size against 3MB partition limit
python ../tools/check_firmware_size.py

# 5. Flash to hardware
idf.py flash monitor
```

---

## Running the Host Bridge

```bash
# 1. Install dependencies
pip install -r tools/requirements.txt

# 2. Dry-run mode (Simulate device screen and BLE packets without hardware)
python tools/token_monitor_bridge.py --dry-run

# 3. Live Bluetooth synchronization
python tools/token_monitor_bridge.py
```

---

## Button Navigation

- **UP Key**: Cycle through screen views:
  - `Home Dashboard` → `Limits Center` → `Tools Breakdown` → `Pet Companion` → `Device Info`
- **DOWN Key**: Scroll breakdown list or cycle sub-pages.
- **Long Press OK**: Open system settings (brightness, unpair BLE, factory reset).
