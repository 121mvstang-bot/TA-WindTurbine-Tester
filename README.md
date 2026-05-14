# WindTurbineTester 風力發電機測試平台

一個用 Qt (C++) 寫的桌面端應用程式,搭配 Arduino 端韌體,用來即時量測小型風力發電機的輸出電壓與機構振動(三軸加速度),並提供圖表顯示、統計分析與 CSV 匯出功能。

> 本專案為「機械工作法特論 (Special Topics in Machine Works)」課程之教學示範。

---

## 功能特色

- **即時串列通訊**:透過 USB 序列埠連線 Arduino,自動偵測可用埠列表。
- **雙圖表即時顯示**:電壓 (V) 與三軸加速度 (m/s²) 分別繪製,X 軸視窗保留最近 500 點。
- **加速度計校正**:取 50 筆樣本計算零點偏移,扣除靜置時的重力分量。
- **量測流程控制**:可設定量測時長,內建倒數計時。
- **統計指標**:Min / Max / Avg / RMS 同步更新。
- **CSV 匯出**:可將量測過程的時序資料完整匯出。
- **Arduino 端強健性**:
  - I2C bus recovery (軟體 toggle SCL 釋放卡死的 slave)
  - MPU6050 連續壞值自動重新初始化
  - AVR Watchdog 4 秒硬體 reset 作為最後防線

---

## 硬體需求

| 元件 | 用途 |
|------|------|
| Arduino UNO / Nano (或相容板) | 主控板 |
| INA3221 | 三通道電壓量測 (本程式使用 CH3) |
| MPU6050 | 六軸 IMU (使用三軸加速度) |
| 小型風力發電機 + 風扇 | 測試樣本 |

預設 I2C 接腳 (UNO/Nano):`SDA=A4`, `SCL=A5`。其他板子請自行修改 `arduino_sensor_fixed.ino` 中的 `PIN_SDA`、`PIN_SCL`。

INA3221 預設位址 `0x41`,程式會自動嘗試 `0x40` ~ `0x43`。

---

## 軟體需求

### PC 端 (Qt 程式)
- **Qt 5.15+ 或 Qt 6.x** (Widgets / SerialPort / PrintSupport)
- **CMake 3.16+**
- **C++17** 編譯器 (MSVC / MinGW / GCC / Clang)
- QCustomPlot 2.1.1 (已包含在原始碼中)

### Arduino 端
- Arduino IDE 1.8+ 或 Arduino IDE 2.x
- 函式庫:
  - `Adafruit MPU6050`
  - `Adafruit Unified Sensor`
  - `Wire` (內建)

---

## 建置與執行

### 1. PC 端

```bash
git clone https://github.com/<你的帳號>/WindTurbineTester.git
cd WindTurbineTester
mkdir build && cd build
cmake ..
cmake --build .
```

或直接用 Qt Creator 開啟 `CMakeLists.txt`,選擇 kit 後按下執行即可。

### 2. Arduino 端

用 Arduino IDE 開啟 `arduino_sensor_fixed/arduino_sensor_fixed.ino`,選擇對應的板子與序列埠,然後上傳。

預設 Baud Rate 為 **115200**。

---

## 使用流程

1. 將 Arduino 透過 USB 接上電腦,啟動 `WindTurbineTester`。
2. 從下拉選單選擇對應的 COM Port,按下 **Connect**。
3. 在靜止狀態下按 **Calibrate**,等待校正完成。
4. 按 **Start Measure** 開始量測,需要結束時可按 **Stop Measure**。
5. 量測結束後,可按 **Save CSV** 將完整資料匯出。

---

## 序列協定

Arduino 每筆樣本輸出格式為 CSV 一行:

```
BusV_V,Accel_X_ms2,Accel_Y_ms2,Accel_Z_ms2
0.523,0.012,-0.034,9.812
...
```

以 `#` 開頭的行為註解 / 狀態訊息,Qt 端會自動忽略。

---

## 專案結構

```
WindTurbineTester/
├── CMakeLists.txt
├── main.cpp
├── mainwindow.h / .cpp / .ui
├── serialportwatcher.h / .cpp
├── qcustomplot.h / .cpp        # 第三方繪圖庫 (GPL v3)
├── arduino_sensor_fixed/
│   └── arduino_sensor_fixed.ino
├── LICENSE
├── README.md
└── .gitignore
```

---

## 第三方授權

本專案使用了 [QCustomPlot](https://www.qcustomplot.com/) 2.1.1,由 Emanuel Eichhammer 開發,以 **GNU GPL v3** 釋出。

因此,本專案整體亦以 **GNU GPL v3** 授權釋出 (詳見 `LICENSE`)。

---

## 授權

Copyright (C) 2026

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

See the [LICENSE](LICENSE) file for full text, or visit <https://www.gnu.org/licenses/gpl-3.0.html>.
