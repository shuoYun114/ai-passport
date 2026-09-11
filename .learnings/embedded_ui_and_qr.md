# 嵌入式副屏与二维码交互经验总结

## 1. 架构选型与内存防御 (ESP32-C3 无 PSRAM)
- **避免双重协议栈冲突**：ESP32-C3 仅有 400KB SRAM，当 NimBLE 蓝牙协议栈运行时，若同时拉起 Wi-Fi SoftAP 建立热点，会由于堆内存枯竭频繁导致看门狗重启或 Guru Meditation。
- **最佳解法**：由上位机守护进程提供轻量微型 Web 服务（Python http.server），单片机屏幕利用极低内存（仅占 200 字节栈内存）的 Richard Moore qrcode.c 实时生成并渲染黑白二维码。手机扫码直接打开上位机局域网网页，修改后经由已有 BLE NUS 通道毫秒级推送到单片机 NVS。

## 2. ESP-IDF 编译防御 (-Werror 严格标准)
- **禁用 Xcode 专属标记**：源码中严禁使用 #pragma mark，否则 GCC 会报错 -Werror=unknown-pragmas。
- **防止无符号数下溢判断**：对 uint8_t 等无符号变量进行 x < 0 比较会触发 -Werror=type-limits，应直接写 x >= size。

## 3. 中文字库严防缺字乱码
- 任何新增界面或弹窗文案，必须事先跑脚本与 uddy_font_zh_14.c 对比，并在 	ools/selected_symbols.txt 中补全后重新调用 lv_font_conv 重新生成点阵字库，保证 100% 覆盖。
