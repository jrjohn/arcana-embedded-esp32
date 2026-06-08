# DNESP32S3 V1.2 完整 Pin Map（正點原子 ATK_DNESP32S3）

> 來源：`3，原理图.zip` 內 `ATK_DNESP32S3_V1.2.pdf` + `DNESP32-S3 IO引脚分配表.xlsx`，
> 經原理圖網表交叉核對（2026-06-05）。xlsx 兩處筆誤已修正，標 ⚠。
> 模組：ESP32-S3-WROOM-1（N16R8，Octal PSRAM）。板尺寸 120 × 62 mm。

## ESP32-S3 GPIO 分配表

| GPIO | 功能 A（RGB LCD 模式） | 功能 B（替代功能） | 說明 |
|---|---|---|---|
| IO0 | BOOT 按鍵 | IIC_INT（XL9555 中斷）/ 1WIRE_DQ（DHT11/DS18B20，P5 跳線選） | 一腳三用，啟動 strapping pin |
| IO1 | LED（紅） | — | 板載 LED |
| IO2 | TF_CS | REMOTE_IN（紅外**接收**）⚠ xlsx「触摸IC的CS」「红外发送」均為筆誤 | 用 TF 卡就沒紅外接收 |
| IO3 | LCD_G5 | I2S_MCLK | |
| IO4 | LCD_DE | OV_D0 | |
| IO5 | LCD_CLK | OV_D1 | |
| IO6 | LCD_B7 | OV_D2 | |
| IO7 | LCD_B6 | OV_D3 | |
| IO8 | LCD_G6 ⚠（xlsx 表格欄寫 G5，說明欄與原理圖均為 G6） | ADC_IN（50K 電位器）/ REMOTE_OUT（紅外發射，P3 跳線選） | |
| IO9 | LCD_G3 | I2S_LRCK | |
| IO10 | LCD_G2 | I2S_SDIN | |
| IO11 | SPI2 MOSI（TF / SPI LCD / 無線共用） | | |
| IO12 | SPI2 SCK（共用，經 R38 51R） | | |
| IO13 | SPI2 MISO（共用） | | |
| IO14 | LCD_R7 | I2S_SDOUT | |
| IO15 | LCD_B5 | OV_D4 | |
| IO16 | LCD_B4 | OV_D5 | |
| IO17 | LCD_B3 | OV_D6 | |
| IO18 | LCD_G7 | OV_D7 | |
| IO19 | USB D-（22R 串阻） | | 原生 USB |
| IO20 | USB D+（22R 串阻） | | 原生 USB |
| IO21 | LCD_R6 | SLCD_CS（SPI LCD 片選） | |
| IO35–37 | **不可用** | | N16R8 Octal PSRAM 佔用（原理圖雖拉到 P1 排針，實際禁用） |
| IO38 | CT_SCL（觸控 I²C） | OV_SCL（相機 SCCB） | |
| IO39 | CT_SDA | OV_SDA | |
| IO40 | CT_INT | LCD_DC（SPI LCD） | |
| IO41 | **IIC_SDA**（I²C0 專用） | | XL9555 / ES8388 / 24C02 / QMA6100P / AP3216C |
| IO42 | **IIC_SCL**（I²C0 專用） | | 同上 |
| IO45 | LCD_R3 | OV_PCLK | |
| IO46 | LCD_G4 | I2S_SCK | |
| IO47 | LCD_R5 | OV_VSYNC | |
| IO48 | LCD_R4 | OV_HREF | |
| RXD0/TXD0 | UART0 | CH340C + P4（RS232/485） | 下載 / log |

## XL9555 擴展 IO 分配（I²C0，A0–A2 接 GND）

| 擴展腳 | 信號 | 擴展腳 | 信號 |
|---|---|---|---|
| IO0_0 | AP_INT（光感中斷） | IO1_0 | LCD_BL（背光） |
| IO0_1 | QMA_INT（加速度中斷） | IO1_1 | CT_RST（觸控復位） |
| IO0_2 | SPK_EN（功放使能） | IO1_2 | SLCD_RST |
| IO0_3 | BEEP（蜂鳴器） | IO1_3 | SLCD_PWR |
| IO0_4 | OV_PWDN | IO1_4 | KEY3 |
| IO0_5 | OV_RESET | IO1_5 | KEY2 |
| IO0_6 | GBC_LED（ATK 模組） | IO1_6 | KEY1 |
| IO0_7 | GBC_KEY | IO1_7 | KEY0 |

## 互斥矩陣（firmware 規劃用）

- **RGB LCD ⟷ 相機 ⟷ I2S 音訊**：三者大面積搶腳。RGB LCD 全上時：相機 8 條資料線、I2S 全部 5 條、SPI LCD CS/DC 都被吃掉 → **RGB LCD 模式下無音訊、無相機**。
- **相機 + I2S 可共存**（不開 RGB LCD 時）— 「小智AI」語音 + 視覺 demo 的組合。
- **SPI LCD + TF + 無線**走同一 SPI2，靠各自 CS 分時，可共存。
- **永遠安全**：IO41/42（I²C0）、IO19/20（USB）、UART0、IO1（LED）。

## 跳線 / 選擇器

- **P5（IO_FUNC SELECT）**：`IIC_INT↔SPI_MISO`、`BOOT↔IO_SEL`、`1WIRE_DQ↔LCD_DC` 路由選擇。
- **P3（ADC&REMOTE_OUT）**：IO8 跳線選 電位器 ADC 或 紅外發射。
- **K1**：電源開關；F1：1A 保險絲。

## 板載晶片速查

| 晶片 | 功能 | 匯流排 |
|---|---|---|
| XL9555 | 16-bit IO 擴展 | I²C0（IO41/42），INT→IO0 |
| ES8388 | 音訊 codec（MIC/耳機/喇叭） | I²S（IO3/46/9/10/14）+ I²C0 控制 |
| MD8002A | 3W 功放（SPK_EN 經 XL9555） | — |
| CH340C | USB-UART + 自動下載（DTR/RTS→Q3/Q4 S8050） | UART0 |
| 24C02 | EEPROM | I²C0 |
| QMA6100P | 三軸加速度 | I²C0 |
| AP3216C | 光感 / 距離 | I²C0 |
| RT9013-33 ×2 | 數位 3.3V / 類比 3.3VA LDO | — |

## 本專案（arcana-embedded-esp32）在此板的配置

| 服務 | ESP32 DevKit（預設） | DNESP32S3（`IDF_TARGET=esp32s3` 自動套用） |
|---|---|---|
| SD 卡（SDSPI, SPI2） | CLK=4 / MOSI=32 / MISO=17 / CS=27 @ 4MHz | **CLK=12 / MOSI=11 / MISO=13 / CS=2 @ 20MHz**（板上 TF 槽） |
| OLED SSD1306 | SCL=22 / SDA=21 | SCL=42 / SDA=41（I²C0；板上無此裝置 → 優雅降級） |
| DHT | GPIO15 | GPIO40（U4 座，需 P5 跳線 1WIRE_DQ↔LCD_DC） |
| WS2812B | GPIO26 | GPIO1（板載紅 LED，RMT 波形呈微亮） |
| 燒錄/監控 | 外接 USB-UART | 原生 USB-C（USB-Serial-JTAG）或 CH340（UART 口） |

建置：`idf.py set-target esp32s3 && idf.py build` — `sdkconfig.defaults.esp32s3`（16MB flash + 8MB octal PSRAM）與 Kconfig per-target 預設自動生效。

## OV5640（ATK-MC5640）韌體設定

esp32-camera pin map：D0–D7 = IO4/5/6/7/15/16/17/18，VSYNC=47、HREF=48、PCLK=45、SIOC=38、SIOD=39；`xclk_pin`/`reset_pin`/`pwdn_pin` = **-1**（XCLK 板載 24MHz 晶振；RESET=XL9555 IO0_5、PWDN=XL9555 IO0_4，init 前先經 I²C0 拉好）。SCCB 位址 0x3C。相機與 RGB LCD 互斥。

轉接板 ATK-OCCAMERA-A 對位：標「2」角 → P2 的 GND/3V3 端（P5 側）；標「17」角 → NC/PWD 端（ALS&PS 銅柱側）。**插反會燒模組。**

## ESP-Prog-2

此板**不需要**：S3 內建 USB-Serial-JTAG 已拉到原生 USB-C（`openocd -f board/esp32s3-builtin.cfg` 零接線）。外部 JTAG 腳 MTCK/MTDO/MTDI/MTMS = IO39/40/41/42 被觸控與 I²C0 佔用且 IO40-42 沒拉出 header — 不可行。ESP-Prog-2 僅可當第二路 UART log：拔 P4 兩個 jumper，TXD→P4.3(U0_RXD)、RXD→P4.4(U0_TXD)、GND 共地。
