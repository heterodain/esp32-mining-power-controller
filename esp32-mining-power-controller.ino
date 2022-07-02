/*
  BH1750FVI と ESP32 の接続:
  VCC  <-> 3V3
  GND  <-> GND
  SDA  <-> GPIO21/SDA 
  SCL  <-> GPIO22/SCL
*/

#include <inttypes.h>
#include <WiFi.h>
#include <BH1750.h>
#include <Ambient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

/* Settings -------------------------------- */

// OCプロファイルの名前
const String HIGH_OC_PROFILE_NAME = "HIGH";
const String LOW_OC_PROFILE_NAME = "LOW";

// Power Limit制御の閾値とヒステリシス (Lux)
const int PL_THRETHOLD = 2800;
const int PL_HYSTERESIS = 300;

// PC電源制御の閾値とヒステリシス (Lux)
const int PC_POWER_THRETHOLD = 1800;
const int PC_POWER_HYSTERESIS = 300;

// PC電源状態の入力ピン
const int PC_POWER_STATUS_PIN = 18;
// PC電源スイッチの出力ピン
const int PC_POWER_SW_PIN = 19;

// Ambientのチャネル接続設定
const int AMBIENT_CHANNEL_ID = 99999;
const char AMBIENT_WRITE_KEY[] = "****************";

// WIFIの接続設定
const char WIFI_SSID[] = "************";
const char WIFI_PASSWORD[] = "**********";
const IPAddress WIFI_IP(192, 168, 116, 221);
const IPAddress WIFI_GATEWAY(192, 168, 116, 254);
const IPAddress WIFI_SUBNET(255, 255, 255, 0);
const IPAddress WIFI_DNS(8, 8, 8, 8);

// Hive APIの接続設定
// [farm-id]: ファームのID
// [worker-id]: ワーカーのID
// [personal-token]: パーソナルトークン(アカウント設定から生成可能)
const char GET_OC_PROFILES_URL[] = "https://api2.hiveos.farm/api/v2/farms/[farm-id]/oc";
const char SET_WORKER_OC_URL[] = "https://api2.hiveos.farm/api/v2/farms/[farm-id]/workers/[worker-id]";
const char PERSONAL_TOKEN[] = "Bearer [personal-token]";

/* ---------------------------------------- */

// Hive OSのOCプロファイル情報
struct OcProfile {
  int id;
  String name;
  int ambientColor;
};

// WIFIクライアント
WiFiClient client;
// BH1750FVI照度センサー
BH1750 lightSensor;
// Ambientクラウドサービス
Ambient ambient;
// HTTPクライアント
HTTPClient httpClient;

// OCプロファイル
OcProfile lowOcProfile;
OcProfile highOcProfile;

// 照度の3分間積算値
float threeMinDataSummary = 0.0;
int threeMinDataCount = 0;

// 照度の15分間積算値
float fifteenMinDataSummary = 0.0;
int fifteenMinDataCount = 0;

// 現在のOCプロファイル
OcProfile currentOcProfile = { 0, "UNKNOWN", 0 };

/**
 * 初期化処理
 */
void setup() {
  // シリアル初期化
  Serial.begin(9600);
  while (!Serial);
  
  Serial.println("Start");

  // GPIO初期化
  pinMode(PC_POWER_STATUS_PIN, INPUT);
  gpio_set_pull_mode(GPIO_NUM_18, GPIO_PULLDOWN_ONLY);
  pinMode(PC_POWER_SW_PIN, OUTPUT);

  // 照度センサー初期化
  Wire.begin();
  lightSensor.begin(BH1750::ONE_TIME_HIGH_RES_MODE);

  // Wifi接続
  WiFi.config(WIFI_IP, WIFI_GATEWAY, WIFI_SUBNET, WIFI_DNS);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("WiFi connecting");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }

  Serial.println(" connected");

  // Ambient初期化
  ambient.begin(AMBIENT_CHANNEL_ID, AMBIENT_WRITE_KEY, &client);

  // OCプロファイル取得
  getOcProfiles();
  Serial.print("LOW OC Profile: id=");
  Serial.print(lowOcProfile.id);
  Serial.print(", name=");
  Serial.println(lowOcProfile.name);
  Serial.print("HIGH OC Profile: id=");
  Serial.print(highOcProfile.id);
  Serial.print(", name=");
  Serial.println(highOcProfile.name);

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
  threeMinDataSummary += lux;
  threeMinDataCount++;

  // PC電源状態取得
  int pcPowerStatus = digitalRead(PC_POWER_STATUS_PIN);
  
  Serial.print("current: lux=");
  Serial.print(lux);
  Serial.print(", pc_power=");
  Serial.println(pcPowerStatus == HIGH ? "ON" : "OFF");

  // 60ループ毎(約3分毎)にAmbient送信
  if (threeMinDataCount >= 60) {
    float average = threeMinDataSummary / threeMinDataCount;
    sendAmbient(average, pcPowerStatus);
     
    fifteenMinDataSummary += average;
    fifteenMinDataCount++;

    threeMinDataSummary = 0.0;
    threeMinDataCount = 0;
  }
  
  // 300ループ毎(約15分毎)にPC電源制御とPower Limit制御
  if (fifteenMinDataCount >= 5) {
    float average = fifteenMinDataSummary / fifteenMinDataCount;

    // 照度が(閾値-ヒステリシス)を下回っていたらPower Limit下げ
    if (average < (PL_THRETHOLD - PL_HYSTERESIS) && lowOcProfile.name.compareTo(currentOcProfile.name) != 0) {
      changeOcProfile(lowOcProfile);
      
    // 照度が(閾値+ヒステリシス)を上回っていたらPower Limit上げ
    } else if (average > (PL_THRETHOLD + PL_HYSTERESIS) && highOcProfile.name.compareTo(currentOcProfile.name) != 0) {
      changeOcProfile(highOcProfile);
    }

    // 照度が(閾値-ヒステリシス)を下回っていたらPC電源オフ
    if (average < (PC_POWER_THRETHOLD - PC_POWER_HYSTERESIS) && pcPowerStatus == HIGH) {
      Serial.println("PC power off");
      pushPcPowerButton();

    // 照度が(閾値+ヒステリシス)を上回っていたらPC電源オン
    } else if (average > (PC_POWER_THRETHOLD + PC_POWER_HYSTERESIS) && pcPowerStatus == LOW) {
      Serial.println("PC power on");
      pushPcPowerButton();
    }
      
    fifteenMinDataSummary = 0.0;
    fifteenMinDataCount = 0;
  }

  delay(3000);
}

/**
 * Ambientへのデータ送信
 */
void sendAmbient(double lux, int pcPowerStatus) {
  Serial.print("send ambient: lux=");
  Serial.print(lux);
  Serial.print(", oc=");
  Serial.print(currentOcProfile.name);
  Serial.print(", pc_power=");
  Serial.println(pcPowerStatus == HIGH ? "ON" : "OFF");

  ambient.set(1, lux);
  ambient.set(2, currentOcProfile.ambientColor);
  ambient.set(3, pcPowerStatus == HIGH ? 1 : 0);
  ambient.send();
}

/**
 * HIGH/LOWのOCプロファイルを取得
 */
void getOcProfiles() {
  Serial.print("request: [GET] ");
  Serial.println(GET_OC_PROFILES_URL);

  httpClient.begin(GET_OC_PROFILES_URL);
  httpClient.addHeader("Authorization", PERSONAL_TOKEN);

  int resCode = httpClient.GET();
  if (resCode == HTTP_CODE_OK) {
    String resData = httpClient.getString();
    Serial.print("response: ");
    Serial.println(resData);

    DynamicJsonDocument jsonDoc(2048);
    deserializeJson(jsonDoc, resData);
    JsonArray json = jsonDoc["data"].as<JsonArray>();
    for (JsonObject data : json) {
      String name = data["name"].as<String>();
      if (HIGH_OC_PROFILE_NAME.compareTo(name) == 0) {
        highOcProfile = { data["id"].as<int>(), name, 9 }; // 9:赤色
      } else if (LOW_OC_PROFILE_NAME.compareTo(name) == 0) {
        lowOcProfile = { data["id"].as<int>(), name, 12 }; // 12:緑色
      }
    }
  } else {
    Serial.print("get oc profiles error: ");
    Serial.println(resCode);
  }
  httpClient.end();
}

/**
 * OCプロファイルを変更
 */
void changeOcProfile(OcProfile ocProfile) {
  Serial.print("request: [PATCH] ");
  Serial.println(GET_OC_PROFILES_URL);

  httpClient.begin(SET_WORKER_OC_URL);
  httpClient.addHeader("Content-Type", "application/json");
  httpClient.addHeader("Authorization", PERSONAL_TOKEN);

  String json = String("{\"oc_id\":") + ocProfile.id + ",\"oc_apply_mode\":\"replace\"}";
  Serial.print("payload: ");
  Serial.println(json);

  char jsonArray[json.length() + 1];
  json.toCharArray(jsonArray, sizeof(jsonArray));
  int resCode = httpClient.PATCH((uint8_t*) jsonArray, strlen(jsonArray));
  if (resCode == HTTP_CODE_OK) {
    Serial.print("change oc profile to ");
    Serial.println(ocProfile.name);
  } else {
    Serial.print("changeOcprofile error: ");
    Serial.println(resCode);
  }
  httpClient.end();

  currentOcProfile = ocProfile;
}

/**
 * PC電源制御
 */
 void pushPcPowerButton() {
    // 0.3秒間電源スイッチを押す
    digitalWrite(PC_POWER_SW_PIN, HIGH);
    delay(300);
    digitalWrite(PC_POWER_SW_PIN, LOW);
 }
