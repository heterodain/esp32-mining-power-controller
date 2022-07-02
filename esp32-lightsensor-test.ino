/*
  BH1750FVI と ESP32 の接続:
  VCC  <-> 3V3
  GND  <-> GND
  SDA  <-> GPIO21/SDA 
  SCL  <-> GPIO22/SCL
*/

#include <inttypes.h>
#include <BH1750.h>

// BH1750FVI照度センサー
BH1750 lightSensor;

/**
 * 初期化処理
 */
void setup() {
  // シリアル初期化
  Serial.begin(9600);
  while (!Serial);
  
  Serial.println("Start");

  // 照度センサー初期化
  Wire.begin();
  lightSensor.begin(BH1750::ONE_TIME_HIGH_RES_MODE);

  delay(1000);
}

/**
 * 3秒毎のループ処理
 */
void loop() {
  // 照度計測
  lightSensor.configure(BH1750::ONE_TIME_HIGH_RES_MODE);
  while (!lightSensor.measurementReady(true)) {
    delay(10);
  }
  float lux = lightSensor.readLightLevel();
  Serial.print("current: lux=");
  Serial.println(lux);

  delay(3000);
}
