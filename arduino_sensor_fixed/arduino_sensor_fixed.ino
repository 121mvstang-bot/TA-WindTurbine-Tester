/*
 * Wind Turbine Tester — Arduino 端 (修正 I2C bus hang)
 *
 *   解決「跑久讀不到 MPU6050，要重開」的問題。
 *   修正重點：
 *     1. Wire.setWireTimeout()  避免 SDA 卡住時無限等待
 *     2. I2C bus recovery       軟體 toggle SCL 9 次釋放卡住的 slave
 *     3. MPU 健康度監控          連續壞值就自動重新 init
 *     4. Watchdog (AVR only)    4 秒沒餵狗就硬體重啟，最後一道防線
 *
 *   I2C 接腳預設為 UNO/Nano (SDA=A4, SCL=A5)。
 *   若用 Mega 改成 20/21；用 ESP32 改成你實際用的 GPIO。
 */

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#ifdef __AVR__
  #include <avr/wdt.h>
#endif

// ============================================================
// 1. 物件 / 常數
// ============================================================
Adafruit_MPU6050 mpu;

#define SERIAL_BAUD       115200
#define INA3221_ADDR_DEF  0x41
#define BUS_SCALE         1.0f
#define VOLT_HZ           25
#define PRINT_HZ          10

// INA3221 bus-voltage registers
#define REG_BUS_CH3       0x06
#define REG_BUS_CH1       0x02
#define REG_BUS_CH2       0x04

// I2C pins (UNO/Nano)
#define PIN_SDA           A4
#define PIN_SCL           A5

// ---- 狀態變數 ----
float    ch3_BusV = NAN;
uint8_t  INA_ADDR = INA3221_ADDR_DEF;

unsigned long msPerVolt   = 1000UL / VOLT_HZ;
unsigned long msPerPrint  = 1000UL / PRINT_HZ;
unsigned long tMillisVolt  = 0;
unsigned long tMillisPrint = 0;

// ---- 健康度監控 ----
unsigned long lastGoodMpuMs    = 0;
int           consecutiveBadReads = 0;
const int     MAX_BAD_READS    = 10;
const unsigned long MPU_TIMEOUT_MS = 2000;  // 2 秒沒拿到正常值就重啟

// ============================================================
// 2. I2C bus recovery
//    當 slave 把 SDA 拉低不放時，用軟體 toggle SCL 9 次把它逼放開
// ============================================================
void i2cBusRecovery() {
  Wire.end();

  pinMode(PIN_SCL, OUTPUT);
  pinMode(PIN_SDA, INPUT_PULLUP);

  // 送出 9 個 clock pulse
  for (int i = 0; i < 9; i++) {
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(PIN_SCL, LOW);
    delayMicroseconds(5);
  }

  // 產生 STOP condition (SDA: LOW → HIGH while SCL=HIGH)
  pinMode(PIN_SDA, OUTPUT);
  digitalWrite(PIN_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_SDA, HIGH);
  delayMicroseconds(5);

  // 重啟 I2C
  Wire.begin();
  Wire.setClock(100000);
#if defined(__AVR__)
  Wire.setWireTimeout(3000, true);  // 3 ms + 自動 reset
#endif
}

// ============================================================
// 3. I2C helpers
// ============================================================
uint16_t i2cRead16(uint8_t dev, uint8_t reg) {
  Wire.beginTransmission(dev);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFFFF;
  if (Wire.requestFrom(dev, (uint8_t)2) != 2) return 0xFFFF;
  uint16_t hi = Wire.read(), lo = Wire.read();
  return (hi << 8) | lo;
}

float readINA3221BusV_at(uint8_t addr, uint8_t regBus) {
  uint16_t raw = i2cRead16(addr, regBus);
  if (raw == 0xFFFF) return NAN;
  raw >>= 3;
  return (raw * 0.008f) * BUS_SCALE;
}

void pickInaAddress() {
  uint8_t candidates[4] = {0x40, 0x41, 0x42, 0x43};

  float test = readINA3221BusV_at(INA3221_ADDR_DEF, REG_BUS_CH1);
  if (!isnan(test)) {
    INA_ADDR = INA3221_ADDR_DEF;
    Serial.print("# INA3221 using 0x"); Serial.println(INA_ADDR, HEX);
    return;
  }
  for (uint8_t i = 0; i < 4; i++) {
    float v = readINA3221BusV_at(candidates[i], REG_BUS_CH1);
    if (!isnan(v)) {
      INA_ADDR = candidates[i];
      Serial.print("# INA3221 fallback to 0x"); Serial.println(INA_ADDR, HEX);
      return;
    }
  }
  Serial.println("# WARN: No INA3221 found at 0x40..0x43");
}

// ============================================================
// 4. MPU init (獨立成函式，方便復原時呼叫)
// ============================================================
bool initMPU() {
  if (!mpu.begin()) return false;
  mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  return true;
}

// ============================================================
// 5. Setup
// ============================================================
void setup() {
#ifdef __AVR__
  wdt_disable();  // 開機時先關 WDT，避免被舊狀態觸發
#endif

  Serial.begin(SERIAL_BAUD);
  while (!Serial) delay(10);

  Wire.begin();
  Wire.setClock(100000);
#if defined(__AVR__)
  Wire.setWireTimeout(3000, true);   // [關鍵] 3 ms timeout + 自動 reset
#endif

  Serial.println("# Starting Sensor Setup...");

  if (!initMPU()) {
    Serial.println("# MPU6050 init failed, trying I2C recovery...");
    i2cBusRecovery();
    delay(100);
    if (!initMPU()) {
      Serial.println("# Still failed. Check wiring. Halting.");
      while (1) delay(10);
    }
  }
  Serial.println("# MPU6050 Initialized.");

  pickInaAddress();

  // CSV 標頭 (Qt 端會自動跳過解析失敗的行，這行不會干擾)
  Serial.println("BusV_V,Accel_X_ms2,Accel_Y_ms2,Accel_Z_ms2");

  tMillisVolt   = millis();
  tMillisPrint  = millis();
  lastGoodMpuMs = millis();

#ifdef __AVR__
  wdt_enable(WDTO_4S);  // 4 秒沒餵狗就硬體 reset
#endif
}

// ============================================================
// 6. Loop
// ============================================================
void loop() {
#ifdef __AVR__
  wdt_reset();   // 餵狗
#endif

  unsigned long nowMs = millis();

  // ---- 讀 INA3221 CH3 電壓 ----
  if (nowMs - tMillisVolt >= msPerVolt) {
    tMillisVolt = nowMs;            // [修正] 不再用 += 追進度
    ch3_BusV = readINA3221BusV_at(INA_ADDR, REG_BUS_CH3);
  }

  // ---- 讀 MPU 並輸出 ----
  if (nowMs - tMillisPrint >= msPerPrint) {
    tMillisPrint = nowMs;            // [修正] 同上

    sensors_event_t a, g, temp;
    bool ok = mpu.getEvent(&a, &g, &temp);

    // 判斷讀值合理性：成功 + 不是三軸全 0 (全 0 通常代表通訊掛掉)
    bool dataValid = ok && !(a.acceleration.x == 0.0f &&
                             a.acceleration.y == 0.0f &&
                             a.acceleration.z == 0.0f);

    if (dataValid) {
      consecutiveBadReads = 0;
      lastGoodMpuMs = nowMs;
    } else {
      consecutiveBadReads++;
    }

    // 連續壞值或太久沒拿到正常值 → 嘗試復原
    if (consecutiveBadReads >= MAX_BAD_READS ||
        (nowMs - lastGoodMpuMs) > MPU_TIMEOUT_MS) {
      Serial.println("# MPU lost, recovering I2C bus...");
      i2cBusRecovery();
      delay(50);
      initMPU();
      pickInaAddress();
      consecutiveBadReads = 0;
      lastGoodMpuMs = nowMs;
      return;  // 跳過這次輸出，下一輪再正常送
    }

    float vout = isnan(ch3_BusV) ? 0.0f : ch3_BusV;

    Serial.print(vout, 3);
    Serial.print(",");
    Serial.print(a.acceleration.x, 3);
    Serial.print(",");
    Serial.print(a.acceleration.y, 3);
    Serial.print(",");
    Serial.println(a.acceleration.z, 3);
  }
}
