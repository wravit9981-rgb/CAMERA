#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <HUSKYLENS.h>
#include <U8g2lib.h>
#include <WiFiEspAT.h>
#include <PubSubClient.h>

/*
 * ================================================================
 *  Arduino Mega 2560 물류 로봇 제어 프로그램 (ESP32 → Mega 포팅판)
 * ================================================================
 *
 *  이 프로그램은 다음 네 가지 기능을 함께 수행합니다.
 *
 *  1. HuskyLens(I2C)로 QR/Tag를 읽어 목적지를 결정합니다.
 *  2. X, Y, Z 스테퍼 모터를 순서대로 움직여 방까지 이동합니다.
 *  3. ESP-01(AT 펌웨어) + WiFiEspAT + MQTT로 Node-RED 원격 명령을 받습니다.
 *  4. U8g2 OLED(I2C)에 현재 동작 상태를 표시합니다.
 *
 *  ESP32 → Mega 로 옮기면서 바뀐 점
 *  - SRAM이 8KB(ESP32는 수백 KB)뿐이므로
 *      · 모든 문자열 상수를 F()/PROGMEM 으로 플래시에 둡니다.
 *      · OLED를 풀 버퍼(1024B) 대신 페이지 버퍼(128B) 모드로 사용합니다.
 *  - I2C 버스(SDA=D20, SCL=D21)를 OLED와 HuskyLens가 공유합니다.
 *      · U8g2는 기본적으로 매 전송마다 Wire.setClock(400kHz)를 호출하므로
 *        HuskyLens용 100kHz 설정이 덮어써집니다. setBusClock(100kHz)로 고정합니다.
 *      · AVR Wire는 버스가 막히면 무한 대기하므로 Wire 타임아웃을 켭니다.
 *      · 부팅 시 SDA/SCL 라인이 LOW로 붙어 있는지 먼저 검사합니다.
 *  - Mega(16MHz AVR)는 ESP32보다 매우 느리고, WiFiEspAT는 UART AT 명령으로
 *    통신하므로 한 번의 MQTT/Wi-Fi 처리에 수 ms~수십 ms가 걸립니다.
 *    모터가 도는 중에는 네트워크/I2C 처리를 멈추고, 동작(A~H) 사이의
 *    모터가 멈춰 있는 순간에만 처리하여 스텝 펄스가 끊기지 않게 합니다.
 *
 *  MQTT 명령 형식
 *  - room:1 ~ room:6 : 해당 방에서 물류 회수
 *  - put:1  ~ put:6  : 해당 방에 물류 투입
 *  - qr:get / qr:put : 다음 QR 인식 시 회수/투입 선택
 *  - home             : 1번 방 동작을 시작하도록 예약된 명령
 *
 *  MQTT 토픽
 *  - dt_robot/command    : Mega가 명령을 받는 토픽
 *  - dt_robot/status     : Mega가 상태를 게시하는 토픽
 *  - dt_robot/connection : 통신 상태(JSON, retained)
 *
 *  배선 (Arduino Mega 2560)
 *  - OLED, HuskyLens : SDA → D20, SCL → D21, GND 공통
 *                      (ESP32의 21/22번 핀 배선을 그대로 옮기면 안 됩니다)
 *  - ESP-01          : Mega TX2(D16) → ESP RX (5V→3.3V 분압 필요)
 *                      Mega RX2(D17) ← ESP TX
 *                      전원은 별도 3.3V 레귤레이터(300mA 이상) 사용,
 *                      Mega의 3.3V 핀(최대 50mA)으로는 부족합니다.
 *  - 스텝 드라이버   : X STEP=7 DIR=6, Z STEP=5 DIR=4, Y STEP=9 DIR=8
 *
 *  주의: 아래 Wi-Fi와 MQTT 설정값은 실제 사용하는 환경에 맞게
 *  반드시 수정해야 합니다.
 */

// Wi-Fi/MQTT 설정을 실제 환경에 맞게 변경하세요.
// 연결할 Wi-Fi 공유기의 이름입니다. ESP-01은 2.4GHz만 지원합니다.
const char WIFI_SSID[] = "head2.4G";
// Wi-Fi 공유기의 비밀번호입니다.
const char WIFI_PASSWORD[] = "$88888888";
// MQTT 브로커의 IP 주소 또는 도메인 이름입니다.
// Node-RED가 실행되는 PC의 IP 주소를 입력할 수 있습니다.
const char MQTT_HOST[] = "192.168.0.63";
// 일반적인 MQTT 기본 포트는 1883입니다. (ESP-01 + Mega 조합은 TLS 미지원)
const uint16_t MQTT_PORT = 1883;
// 브로커에서 인증을 사용하지 않으면 두 값을 빈 문자열로 둡니다.
const char MQTT_USER[] = "";
const char MQTT_PASSWORD[] = "";
// Node-RED MQTT Out 노드는 이 토픽으로 명령을 발행해야 합니다.
const char MQTT_COMMAND_TOPIC[] = "dt_robot/command";
// Mega의 상태를 Node-RED MQTT In 노드에서 읽는 토픽입니다.
const char MQTT_STATUS_TOPIC[] = "dt_robot/status";
// Separate retained telemetry topic for the Node-RED communication indicators.
const char MQTT_CONNECTION_TOPIC[] = "dt_robot/connection";

// ESP-01 AT 펌웨어의 UART 속도입니다. 대부분 115200이 기본값입니다.
const unsigned long ESP_AT_BAUD = 115200UL;

WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
unsigned long lastMqttAttempt = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastWifiStatusCheck = 0;
bool wifiConnected = false;
bool wasWifiConnected = false;
bool wasMqttConnected = false;
bool espAtReady = false;
bool huskylensReady = false;     // begin() + 알고리즘 설정까지 끝났는지
bool huskylensI2cOk = false;     // 마지막 통신 성공 여부
bool oledI2cDetected = false;
bool huskylensI2cDetected = false;
uint8_t oledI2cAddress = 0x3C;
unsigned long lastConnectionPublish = 0;
unsigned long lastI2cCheck = 0;
unsigned long lastHuskyInitAttempt = 0;

// Arduino Mega I2C: SDA=20, SCL=21
// 페이지 버퍼(_1_) 모드: RAM 128바이트만 사용합니다. (_F_ 는 1024바이트)
U8G2_SSD1306_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
// 1.3인치 SH1106 OLED라면 위 줄 대신 아래 줄을 사용하세요.
// U8G2_SH1106_128X64_NONAME_1_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// 1.8° 모터(풀스텝 200) + A4988 1/16 분주: 200 * 16 = 3200 펄스/회전.
// 회전 이동량은 REVOLUTIONS 상수로 유지하고, 드라이버 STEP 펄스 수만 분주에 맞춥니다.
const int STEPS_PER_REVOLUTION = 3200;
// 단위는 steps/sec 및 steps/sec^2입니다.
// 참고: 16MHz Mega는 소프트웨어 펄스 생성으로 약 6000~8000 steps/sec가 한계입니다.
// 더 큰 값을 넣어도 오류는 없지만 실제로는 그 한계 속도까지만 나옵니다.
const float MAX_SPEED = 12000.0f;
const float ACCELERATION = 2400.0f;
const float MIN_SPEED = 200.0f;

// Y축만 느리게 움직이기 위한 별도 속도 설정입니다.
const float Y_MAX_SPEED = 1000.0f;
const float Y_ACCELERATION = 500.0f;
const float Y_MIN_SPEED = 700.0f;
const float A_Y_FORWARD_REVOLUTIONS = 0.6f;
const float B_Z_FORWARD_REVOLUTIONS = 2.3f;
const float C_Y_REVERSE_REVOLUTIONS = 0.6f;
const float D_Z_REVERSE_REVOLUTIONS = 2.3f;
const float E_X_FORWARD_REVOLUTIONS = 15.0f;
// Rooms 1/3/5 are left, 2/4/6 are right; rooms 1/2 are floor 1,
// rooms 3/4 floor 2, and rooms 5/6 floor 3. G reverses F to return.
const float F_ROOM_X_REVOLUTIONS[6] = {0.0f, 2.5f, 0.0f, 2.5f, 0.0f, 2.5f};
const float G_ROOM_X_REVOLUTIONS[6] = {0.0f, -2.5f, 0.0f, -2.5f, 0.0f, -2.5f};
// Z축 가용 범위 26회전을 3개 층으로 나눕니다.
// 1층=0회전, 2층=26/3회전, 3층=52/3회전입니다.
const float Z_REVOLUTIONS_PER_FLOOR = 26.0f / 3.0f;
const float F_ROOM_Z_REVOLUTIONS[6] = {
    0.0f, 0.0f,
    Z_REVOLUTIONS_PER_FLOOR, Z_REVOLUTIONS_PER_FLOOR,
    Z_REVOLUTIONS_PER_FLOOR * 2.0f, Z_REVOLUTIONS_PER_FLOOR * 2.0f};
const float G_ROOM_Z_REVOLUTIONS[6] = {
    0.0f, 0.0f,
    -Z_REVOLUTIONS_PER_FLOOR, -Z_REVOLUTIONS_PER_FLOOR,
    -Z_REVOLUTIONS_PER_FLOOR * 2.0f, -Z_REVOLUTIONS_PER_FLOOR * 2.0f};
const float H_X_REVERSE_REVOLUTIONS = 15.0f;

// Arduino Mega motor driver STEP/DIR pins
const uint8_t stepPinX = 7, dirPinX = 6;
const uint8_t stepPinZ = 5, dirPinZ = 4;
const uint8_t stepPinY = 9, dirPinY = 8;

HUSKYLENS huskylens;

// Arduino Mega 2560 I2C pins: SDA = D20, SCL = D21
// HuskyLens must also be configured as I2C in its Protocol Type menu.
const uint32_t I2C_CLOCK = 100000UL;
const uint8_t I2C_SDA_PIN = 20;
const uint8_t I2C_SCL_PIN = 21;
const uint8_t HUSKYLENS_I2C_ADDRESS = 0x32;

struct StepperProfile {
  // currentPos와 targetPos는 모터의 논리적인 현재 위치와 목표 위치입니다.
  // 실제 엔코더가 있는 것이 아니므로 전원이 켜질 때 현재 위치를 0으로
  // 가정합니다. 따라서 시작할 때 반드시 기계적인 원점에 있어야 합니다.
  long currentPos = 0, targetPos = 0, totalSteps = 0, stepCount = 0;
  // 가감속을 위해 현재 속도와 다음 스텝까지의 간격을 저장합니다.
  float currentSpeed = MIN_SPEED;
  unsigned long lastStepMicros = 0, stepIntervalMicros = 10000;
  // 이 모터에 적용할 속도 설정입니다. (Y축만 느리게)
  float maxSpeed = MAX_SPEED, acceleration = ACCELERATION, minSpeed = MIN_SPEED;
  // STEP 핀을 digitalWrite보다 빠르게 토글하기 위한 포트 레지스터/비트입니다.
  volatile uint8_t *stepPort = nullptr;
  uint8_t stepMask = 0;
};

StepperProfile motorX, motorZ, motorY;

enum MotionAction : uint8_t { ACTION_A, ACTION_B, ACTION_C, ACTION_D,
                              ACTION_E, ACTION_F, ACTION_G, ACTION_H };
const MotionAction PUT_SEQUENCE[] = {
    ACTION_A, ACTION_B, ACTION_C, ACTION_D, ACTION_E, ACTION_F,
    // 방에 도착하면 먼저 Z축을 조금 올린 뒤 Y축으로 진입하고,
    // Z축을 내려 물건을 놓은 다음 Y축을 복귀합니다.
    ACTION_B, ACTION_A, ACTION_D, ACTION_C, ACTION_H, ACTION_G};
const MotionAction GET_SEQUENCE[] = {
    ACTION_E, ACTION_F, ACTION_A, ACTION_B, ACTION_C, ACTION_D,
    ACTION_H, ACTION_G, ACTION_B, ACTION_A, ACTION_D, ACTION_C};

// sequenceState는 로봇의 현재 작업 단계를 나타내는 상태 머신입니다.
// 0: QR/MQTT 명령 대기
// 1: A~H 동작 시퀀스 실행 중
// 9: 완료 후 잠시 대기
int sequenceState = 0;
const MotionAction *activeSequence = nullptr;
size_t activeSequenceLength = 0;
size_t activeActionIndex = 0;
uint8_t activeActionPhase = 0;
StepperProfile *activeMotor = nullptr;
int selectedRoom = -1;
int currentItemId = -1;
// false이면 회수, true이면 투입 작업입니다.
bool mqttPutMode = false;
// Node-RED selects the action for the next QR scan.
bool qrPutMode = false;
// 방별 적재 상태입니다. 센서를 사용하지 않고 프로그램 변수만으로 관리합니다.
// 인덱스 0~5는 각각 1~6번 방을 의미합니다.
// true  = 적재됨, false = 비어 있음
bool roomLoaded[6] = {false, false, false, false, false, false};
unsigned long waitStartTime = 0;

void startRoom(int room);
void showMessage(const __FlashStringHelper *line1, const __FlashStringHelper *line2 = nullptr);
void showMessage(const __FlashStringHelper *line1, const char *line2);
void publishItemStatus(const char *status);

// ---------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------

void publishStatus(const char *status) {
  // MQTT 연결이 끊긴 상태에서는 publish를 시도하지 않습니다.
  // 마지막 인자인 true는 retained 메시지를 의미합니다.
  if (mqttClient.connected()) mqttClient.publish(MQTT_STATUS_TOPIC, status, true);
}

void publishConnectionStatus(bool force = false) {
  if (!mqttClient.connected()) return;
  unsigned long now = millis();
  if (!force && now - lastConnectionPublish < 2000) return;
  lastConnectionPublish = now;

  char message[96];
  IPAddress ip = WiFi.localIP();
  snprintf_P(message, sizeof(message),
             PSTR("{\"wifi\":%s,\"mqtt\":true,\"i2c\":%s,\"ip\":\"%u.%u.%u.%u\"}"),
             wifiConnected ? "true" : "false",
             huskylensI2cOk ? "true" : "false",
             ip[0], ip[1], ip[2], ip[3]);
  mqttClient.publish(MQTT_CONNECTION_TOPIC, message, true);
}

bool connectMqttWithLastWill(const char *clientId) {
  // Retained last-will message: the broker publishes it if the Mega loses
  // its MQTT connection without sending a clean disconnect.
  static const char offlineMessage[] =
      "{\"wifi\":null,\"mqtt\":false,\"i2c\":null,\"online\":false}";
  bool connected = MQTT_USER[0] == '\0'
      ? mqttClient.connect(clientId, MQTT_CONNECTION_TOPIC, 1, true, offlineMessage)
      : mqttClient.connect(clientId, MQTT_USER, MQTT_PASSWORD,
                           MQTT_CONNECTION_TOPIC, 1, true, offlineMessage);
  if (!connected) return false;

  if (mqttClient.subscribe(MQTT_COMMAND_TOPIC)) {
    wasMqttConnected = true;
    publishConnectionStatus(true);
    publishStatus("online");
    Serial.println(F("MQTT connected, command topic subscribed, and LWT configured"));
    return true;
  }
  Serial.println(F("MQTT subscribe failed; disconnecting to retry"));
  mqttClient.disconnect();
  wasMqttConnected = false;
  return false;
}

void reportRoomStateConflict(int room, bool putting) {
  if (putting) {
    Serial.print(F("이미 물건이 적재되어 있습니다. (room "));
    Serial.print(room + 1); Serial.println(')');
    showMessage(F("ALREADY LOADED"), F("CANNOT PUT ITEM"));
    publishStatus("put_rejected_already_loaded");
  } else {
    Serial.print(F("이미 물건이 회수되어 있습니다. (room "));
    Serial.print(room + 1); Serial.println(')');
    showMessage(F("ALREADY RETRIEVED"), F("ITEM IS GONE"));
    publishStatus("get_rejected_already_retrieved");
  }
}

void mqttCallback(char *, byte *payload, unsigned int length) {
  // payload는 문자열 종료 문자(\0)가 보장되지 않으므로 직접 복사합니다.
  char command[32];
  unsigned int maxLength = sizeof(command) - 1;
  unsigned int n = length < maxLength ? length : maxLength;
  memcpy(command, payload, n);
  command[n] = '\0';
  Serial.print(F("MQTT command received: ")); Serial.println(command);

  // Mode changes are safe during motion because they affect only the next QR scan.
  if (strcmp_P(command, PSTR("qr:get")) == 0) {
    qrPutMode = false;
    publishStatus("qr_mode_retrieve");
    showMessage(F("QR MODE"), F("RETRIEVE ITEM"));
    return;
  }
  if (strcmp_P(command, PSTR("qr:put")) == 0) {
    qrPutMode = true;
    publishStatus("qr_mode_load");
    showMessage(F("QR MODE"), F("LOAD ITEM"));
    return;
  }

  if (sequenceState != 0) {
    Serial.print(F("MQTT command ignored: robot busy (state="));
    Serial.print(sequenceState); Serial.println(')');
    return;
  }

  if (strncmp_P(command, PSTR("room:"), 5) == 0) {
    // room:N은 물류 회수 명령입니다.
    int room = atoi(command + 5) - 1;
    if (room >= 0 && room < 6) {
      // 회수할 물건이 없는 방이면 모터를 움직이지 않습니다.
      if (!roomLoaded[room]) {
        reportRoomStateConflict(room, false);
        return;
      }
      mqttPutMode = false;
      startRoom(room);
    }
  } else if (strncmp_P(command, PSTR("put:"), 4) == 0) {
    // put:N은 물류 투입 명령입니다.
    int room = atoi(command + 4) - 1;
    if (room >= 0 && room < 6) {
      // 이미 적재된 방에는 다시 물류를 넣지 않습니다.
      if (roomLoaded[room]) {
        reportRoomStateConflict(room, true);
        return;
      }
      mqttPutMode = true;
      startRoom(room);
    }
  } else if (strcmp_P(command, PSTR("home")) == 0) {
    // 현재 프로젝트의 home 명령은 1번 위치 작업을 시작하는 명령입니다.
    mqttPutMode = false;
    startRoom(0);
  }
}

void connectMqtt() {
  bool connected = mqttClient.connected();
  if (wasMqttConnected && !connected) {
    Serial.println(F("MQTT disconnected; reconnecting..."));
  }
  wasMqttConnected = connected;
  if (!espAtReady || !wifiConnected || connected) return;
  // 5초마다 한 번만 재접속해 네트워크 장애 시 무한 접속 시도를 방지합니다.
  if (millis() - lastMqttAttempt < 5000) return;
  lastMqttAttempt = millis();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char clientId[24];
  snprintf_P(clientId, sizeof(clientId), PSTR("dt-robot-%02X%02X%02X%02X%02X%02X"),
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  if (!connectMqttWithLastWill(clientId)) {
    Serial.print(F("MQTT connection failed, state="));
    Serial.println(mqttClient.state());
  }
}

// ---------------------------------------------------------------
// Wi-Fi (ESP-01 AT 펌웨어 + WiFiEspAT)
// ---------------------------------------------------------------

// WiFi.init() 실패 시 원인을 구분합니다.
// "AT"에 응답이 없으면 배선/전원/baud 문제, 응답은 있는데 init이 실패하면
// WiFiEspAT가 요구하는 AT 1.7+ 명령(AT+CIPRECVMODE)을 지원하지 않는 구형 펌웨어입니다.
void explainEspFailure() {
  while (Serial2.available()) Serial2.read();
  Serial2.print(F("AT\r\n"));
  unsigned long start = millis();
  bool anyByte = false, ok = false;
  char prev = 0;
  while (millis() - start < 500) {
    if (!Serial2.available()) continue;
    char c = Serial2.read();
    anyByte = true;
    if (prev == 'O' && c == 'K') ok = true;
    prev = c;
  }
  if (ok) {
    Serial.println(F("ESP-01 answers AT, but WiFiEspAT init failed: AT firmware is too old "
                     "(needs AT 1.7+). Run the 'diagnose' env to see AT+GMR."));
  } else if (anyByte) {
    Serial.println(F("ESP-01 sends garbage: baud mismatch. Run the 'diagnose' env and set ESP_AT_BAUD."));
  } else {
    Serial.println(F("ESP-01 sends nothing: check TX2(D16)->ESP RX, RX2(D17)<-ESP TX, "
                     "3.3V supply >=300mA, CH_PD/EN and RST tied to 3.3V, common GND."));
  }
}

bool initEspAt() {
  Serial2.begin(ESP_AT_BAUD);
  espAtReady = WiFi.init(Serial2);
  if (!espAtReady) explainEspFailure();
  if (espAtReady) {
    Serial.println(F("ESP-01 AT module detected"));
    char fw[16];
    if (WiFi.firmwareVersion(fw)) {
      Serial.print(F("ESP-01 AT firmware: ")); Serial.println(fw);
    }
    // WiFiEspAT의 WiFi.begin()은 연결 완료(또는 실패)까지 기다립니다.
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  return espAtReady;
}

void maintainWifi() {
  if (!espAtReady) {
    if (millis() - lastWifiAttempt >= 10000) {
      lastWifiAttempt = millis();
      Serial.println(F("ESP-01 AT not ready; retrying module detection..."));
      if (!initEspAt()) Serial.println(F("ESP-01 still not responding to AT commands"));
    }
    return;
  }

  // WiFi.status()는 매번 AT 명령을 보내므로 1초에 한 번만 확인합니다.
  if (millis() - lastWifiStatusCheck < 1000) return;
  lastWifiStatusCheck = millis();
  wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected) {
    if (!wasWifiConnected) {
      Serial.print(F("Wi-Fi connected, IP: "));
      Serial.println(WiFi.localIP());
    }
    wasWifiConnected = true;
    return;
  }

  if (wasWifiConnected) {
    Serial.print(F("Wi-Fi disconnected, status="));
    Serial.println((int)WiFi.status());
    wasWifiConnected = false;
    wasMqttConnected = false;
    // Close the old TCP socket so MQTT starts with a clean connection after Wi-Fi returns.
    mqttClient.disconnect();
  }
  if (millis() - lastWifiAttempt >= 10000) {
    lastWifiAttempt = millis();
    Serial.println(F("Retrying Wi-Fi connection..."));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void setupNetwork() {
  Serial.println(F("Checking ESP-01 AT response on Serial2 (TX2=D16, RX2=D17)..."));
  if (!initEspAt()) {
    Serial.println(F("ESP-01 AT module not detected; check UART wiring (TX/RX cross), "
                     "3.3V power (>=300mA), baud rate, and AT firmware"));
  }
  Serial.print(F("Connecting Wi-Fi"));
  unsigned long started = millis();
  // 부팅을 무한정 멈추지 않도록 Wi-Fi 연결은 최대 15초만 기다립니다.
  while (espAtReady && WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250); Serial.print('.');
  }
  Serial.println();
  wifiConnected = espAtReady && WiFi.status() == WL_CONNECTED;
  wasWifiConnected = wifiConnected;
  if (wifiConnected) {
    Serial.print(F("Wi-Fi connected, IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("Wi-Fi connection timed out"));
  }
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);
  // WiFiEspAT는 응답이 느리므로 소켓 타임아웃을 넉넉히 줍니다. (초)
  mqttClient.setSocketTimeout(5);
  Serial.print(F("MQTT broker configured: ")); Serial.print(MQTT_HOST);
  Serial.print(':'); Serial.println(MQTT_PORT);
}

// 네트워크와 I2C 상태 확인을 한 번에 처리합니다.
// 모터가 정지해 있을 때만 호출해야 합니다. (처리 중에는 스텝 펄스를 낼 수 없음)
void serviceBackground() {
  maintainWifi();
  connectMqtt();
  if (mqttClient.connected() && !mqttClient.loop()) {
    Serial.println(F("MQTT loop detected connection loss"));
    wasMqttConnected = false;
  }
  publishConnectionStatus();
}

// ---------------------------------------------------------------
// OLED / I2C
// ---------------------------------------------------------------

// 페이지 버퍼 모드에서는 firstPage()/nextPage() 루프 안에서 그려야 합니다.
// 버퍼 크기를 아끼기 위해 줄은 최대 21자(128px / 6px)까지 복사합니다.
void drawTwoLines(const char *line1, const char *line2) {
  oled.firstPage();
  do {
    oled.setFont(u8g2_font_6x10_tf);
    oled.drawStr(2, 20, line1);
    if (line2 != nullptr && line2[0] != '\0') oled.drawStr(2, 45, line2);
  } while (oled.nextPage());
}

void showMessage(const __FlashStringHelper *line1, const char *line2) {
  char buf1[22];
  strncpy_P(buf1, (const char *)line1, sizeof(buf1) - 1);
  buf1[sizeof(buf1) - 1] = '\0';
  drawTwoLines(buf1, line2);
}

void showMessage(const __FlashStringHelper *line1, const __FlashStringHelper *line2) {
  char buf2[22] = "";
  if (line2 != nullptr) {
    strncpy_P(buf2, (const char *)line2, sizeof(buf2) - 1);
    buf2[sizeof(buf2) - 1] = '\0';
  }
  showMessage(line1, buf2);
}

// 장치가 전원이 꺼진 채로 버스에 연결돼 있거나 배선이 GND에 닿아 있으면
// SDA/SCL이 LOW로 붙어 버려 I2C 전체(OLED 포함)가 동작하지 않습니다.
// Wire.begin() 전에 라인 상태를 먼저 확인합니다.
void checkI2cLines() {
  pinMode(I2C_SDA_PIN, INPUT);
  pinMode(I2C_SCL_PIN, INPUT);
  delay(2);
  bool sdaHigh = digitalRead(I2C_SDA_PIN) == HIGH;
  bool sclHigh = digitalRead(I2C_SCL_PIN) == HIGH;
  Serial.print(F("I2C idle lines: SDA(D20)="));
  Serial.print(sdaHigh ? F("HIGH") : F("LOW"));
  Serial.print(F(", SCL(D21)="));
  Serial.println(sclHigh ? F("HIGH") : F("LOW"));
  if (!sdaHigh || !sclHigh) {
    Serial.println(F("WARNING: I2C line stuck LOW. Check that every I2C device is powered, "
                     "GND is shared, and SDA/SCL are not swapped or shorted."));
  }
}

void scanI2cBus() {
  bool foundDevice = false;
  oledI2cDetected = false;
  huskylensI2cDetected = false;
  Serial.println(F("Scanning I2C bus..."));
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 5) {
      Serial.println(F("I2C timeout during scan; bus is not responding"));
      break;
    }
    if (error == 0) {
      Serial.print(F("I2C device found at 0x"));
      if (address < 0x10) Serial.print('0');
      Serial.println(address, HEX);
      foundDevice = true;
      if (address == HUSKYLENS_I2C_ADDRESS) huskylensI2cDetected = true;
      if (address == 0x3C || address == 0x3D) {
        oledI2cDetected = true;
        oledI2cAddress = address;
      }
    }
  }
  if (!foundDevice) Serial.println(F("No I2C devices found (check SDA=D20, SCL=D21, VCC, GND)"));
  Serial.print(F("OLED I2C: "));
  if (oledI2cDetected) {
    Serial.print(F("DETECTED at 0x")); Serial.println(oledI2cAddress, HEX);
  } else {
    Serial.println(F("NOT DETECTED (expected 0x3C or 0x3D)"));
  }
  Serial.print(F("HuskyLens I2C: "));
  Serial.println(huskylensI2cDetected ? F("DETECTED at 0x32") : F("NOT DETECTED (expected 0x32)"));
}

void setupI2c() {
  checkI2cLines();
  Wire.begin();
  Wire.setClock(I2C_CLOCK);
  // AVR Wire는 버스가 막히면 영원히 멈춥니다. 25ms 타임아웃 후 자동 리셋합니다.
  Wire.setWireTimeout(25000, true);
  scanI2cBus();
}

// ---------------------------------------------------------------
// HuskyLens
// ---------------------------------------------------------------

bool initHuskylens() {
  if (!huskylens.begin(Wire)) return false;
  // This HuskyLens library exposes QR/tag recognition as TAG_RECOGNITION.
  // Train/register the QR codes in HuskyLens first.
  huskylens.writeAlgorithm(ALGORITHM_TAG_RECOGNITION);
  huskylensReady = true;
  huskylensI2cOk = true;
  return true;
}

int readQrRoom() {
  // HuskyLens에서 검출 결과를 읽고 Tag ID 1~6을 방 번호 1~6으로 변환합니다.
  if (!huskylensReady) return -1;
  // 요청에 실패하면 센서가 연결되지 않았거나 통신 중인 것이므로 -1을 반환합니다.
  if (!huskylens.request()) {
    huskylensI2cOk = false;
    return -1;
  }
  huskylensI2cOk = true;
  // 읽을 수 있는 결과가 없으면 아직 QR/Tag가 감지되지 않은 것입니다.
  if (!huskylens.available()) return -1;
  HUSKYLENSResult result = huskylens.read();
  currentItemId = result.ID;
  // This library represents QR/tag detections as a returned block.
  if (result.command != COMMAND_RETURN_BLOCK) return -1;
  if (result.ID >= 1 && result.ID <= 6) return result.ID - 1;
  return -1;
}

void maintainHuskylens() {
  // 부팅 때 HuskyLens가 없었어도 나중에 연결되면 자동으로 초기화합니다.
  if (!huskylensReady) {
    if (millis() - lastHuskyInitAttempt < 3000) return;
    lastHuskyInitAttempt = millis();
    if (initHuskylens()) {
      Serial.println(F("HuskyLens I2C connected"));
      showMessage(F("READY"), F("SHOW QR CODE"));
    }
    return;
  }
  // 대기 상태에서는 readQrRoom()이 계속 통신하므로 별도 확인이 필요 없습니다.
  if (sequenceState == 0) return;
  unsigned long now = millis();
  if (now - lastI2cCheck < 2000) return;
  lastI2cCheck = now;
  huskylensI2cOk = huskylens.request();
}

// ---------------------------------------------------------------
// Stepper motors
// ---------------------------------------------------------------

void setupMotor(StepperProfile &motor, uint8_t stepPin, uint8_t dirPin,
                float maxSpeed, float acceleration, float minSpeed) {
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  digitalWrite(stepPin, LOW);
  motor.stepPort = portOutputRegister(digitalPinToPort(stepPin));
  motor.stepMask = digitalPinToBitMask(stepPin);
  motor.maxSpeed = maxSpeed;
  motor.acceleration = acceleration;
  motor.minSpeed = minSpeed;
  motor.currentSpeed = minSpeed;
}

void moveTo(StepperProfile &motor, long target, uint8_t dirPin) {
  // 모터를 즉시 구동하지 않고 목표 위치와 방향만 설정합니다.
  // 실제 스텝 출력은 loop()에서 runStepper()가 수행합니다.
  motor.targetPos = target;
  motor.totalSteps = labs(target - motor.currentPos);
  motor.stepCount = 0;
  motor.currentSpeed = motor.minSpeed;
  // 속도는 초당 스텝 수이므로, 한 스텝 간격(마이크로초)은 1,000,000/속도입니다.
  motor.stepIntervalMicros = (unsigned long)(1000000.0f / motor.minSpeed);
  // 목표 위치가 현재 위치보다 크면 정방향, 작으면 역방향으로 설정합니다.
  digitalWrite(dirPin, target >= motor.currentPos ? HIGH : LOW);
  // A4988은 DIR 변경 후 STEP까지 최소 200ns가 필요합니다.
  delayMicroseconds(5);
  motor.lastStepMicros = micros();
}

bool startMotionAction(MotionAction action, uint8_t phase = 0) {
  float revolutions = 0.0f;
  uint8_t dirPin = 0;
  activeMotor = nullptr;

  switch (action) {
    case ACTION_A:
      activeMotor = &motorY; dirPin = dirPinY;
      revolutions = A_Y_FORWARD_REVOLUTIONS; break;
    case ACTION_B:
      activeMotor = &motorZ; dirPin = dirPinZ;
      revolutions = B_Z_FORWARD_REVOLUTIONS; break;
    case ACTION_C:
      activeMotor = &motorY; dirPin = dirPinY;
      revolutions = -C_Y_REVERSE_REVOLUTIONS; break;
    case ACTION_D:
      activeMotor = &motorZ; dirPin = dirPinZ;
      revolutions = -D_Z_REVERSE_REVOLUTIONS; break;
    case ACTION_E:
      activeMotor = &motorX; dirPin = dirPinX;
      revolutions = E_X_FORWARD_REVOLUTIONS; break;
    case ACTION_F:
      if (phase == 0) {
        activeMotor = &motorX; dirPin = dirPinX;
        revolutions = F_ROOM_X_REVOLUTIONS[selectedRoom];
      } else {
        activeMotor = &motorZ; dirPin = dirPinZ;
        revolutions = F_ROOM_Z_REVOLUTIONS[selectedRoom];
      }
      break;
    case ACTION_G:
      if (phase == 0) {
        activeMotor = &motorZ; dirPin = dirPinZ;
        revolutions = G_ROOM_Z_REVOLUTIONS[selectedRoom];
      } else {
        activeMotor = &motorX; dirPin = dirPinX;
        revolutions = G_ROOM_X_REVOLUTIONS[selectedRoom];
      }
      break;
    case ACTION_H:
      activeMotor = &motorX; dirPin = dirPinX;
      revolutions = -H_X_REVERSE_REVOLUTIONS; break;
  }

  if (activeMotor == nullptr) return false;
  long deltaSteps = lround(revolutions * STEPS_PER_REVOLUTION);
  Serial.print(F("Action ")); Serial.print((char)('A' + (int)action));
  Serial.print(F(" phase ")); Serial.print(phase + 1);
  Serial.print(F(": ")); Serial.print(revolutions, 3);
  Serial.println(F(" revolutions"));
  // 시리얼 출력이 끝난 뒤 모터를 시작해야 첫 스텝 타이밍이 흔들리지 않습니다.
  Serial.flush();
  moveTo(*activeMotor, activeMotor->currentPos + deltaSteps, dirPin);
  return true;
}

void calculateNextStep(StepperProfile &motor) {
  // 남은 스텝 수를 기준으로 가속/감속 속도를 계산합니다.
  // AVR은 float 연산이 느리므로(나눗셈/제곱근 각각 약 30us) 계산을 줄였습니다.
  motor.stepCount++;
  long remaining = motor.totalSteps - motor.stepCount;
  if (remaining <= 0) return;

  // 가속: v += a * dt, dt = 1/v 이므로 v += a / v (원래 식과 같은 값)
  float speed = motor.currentSpeed + motor.acceleration / motor.currentSpeed;
  if (speed > motor.maxSpeed) speed = motor.maxSpeed;
  // 감속: v^2 <= 2*a*남은거리. 필요할 때만 제곱근을 계산합니다.
  float brakeLimitSq = 2.0f * motor.acceleration * (float)remaining;
  if (speed * speed > brakeLimitSq) speed = sqrtf(brakeLimitSq);
  if (speed < motor.minSpeed) speed = motor.minSpeed;
  motor.currentSpeed = speed;
  motor.stepIntervalMicros = (unsigned long)(1000000.0f / speed);
}

bool runStepper(StepperProfile &motor) {
  // delay()를 사용하지 않고, 다음 스텝 시간이 되었을 때만 펄스를 냅니다.
  if (motor.currentPos == motor.targetPos) return true;
  unsigned long now = micros();
  if (now - motor.lastStepMicros < motor.stepIntervalMicros) return false;
  motor.lastStepMicros = now;
  // STEP 핀에 짧은 HIGH 펄스를 보냅니다. (A4988 최소 1us)
  uint8_t oldSREG = SREG;
  cli();
  *motor.stepPort |= motor.stepMask;
  SREG = oldSREG;
  delayMicroseconds(2);
  oldSREG = SREG;
  cli();
  *motor.stepPort &= ~motor.stepMask;
  SREG = oldSREG;
  // 펄스를 보냈으므로 논리 위치도 한 스텝 갱신합니다.
  motor.currentPos += (motor.targetPos > motor.currentPos) ? 1 : -1;
  calculateNextStep(motor);
  return motor.currentPos == motor.targetPos;
}

void startRoom(int room) {
  // 선택된 방으로 이동하는 전체 시퀀스를 시작합니다.
  selectedRoom = room;
  activeSequence = mqttPutMode ? PUT_SEQUENCE : GET_SEQUENCE;
  activeSequenceLength = mqttPutMode
      ? sizeof(PUT_SEQUENCE) / sizeof(PUT_SEQUENCE[0])
      : sizeof(GET_SEQUENCE) / sizeof(GET_SEQUENCE[0]);
  activeActionIndex = 0;
  activeActionPhase = 0;

  char floorText[16];
  snprintf_P(floorText, sizeof(floorText), PSTR("%S %d"),
             mqttPutMode ? PSTR("PUT FLOOR") : PSTR("GET FLOOR"), room + 1);
  showMessage(mqttPutMode ? F("PUTTING ITEM") : F("GETTING ITEM"), floorText);
  Serial.print(F("Selected room: "));
  Serial.println(room + 1);

  if (!startMotionAction(activeSequence[activeActionIndex], activeActionPhase)) {
    Serial.println(F("Could not start motion sequence"));
    activeSequence = nullptr;
    activeSequenceLength = 0;
    sequenceState = 0;
    return;
  }
  sequenceState = 1;
}

// ---------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------

void setup() {
  // 모터 핀을 가장 먼저 LOW로 고정해 부팅 중 드라이버가 튀지 않게 합니다.
  setupMotor(motorX, stepPinX, dirPinX, MAX_SPEED, ACCELERATION, MIN_SPEED);
  setupMotor(motorZ, stepPinZ, dirPinZ, MAX_SPEED, ACCELERATION, MIN_SPEED);
  setupMotor(motorY, stepPinY, dirPinY, Y_MAX_SPEED, Y_ACCELERATION, Y_MIN_SPEED);

  Serial.begin(115200);
  delay(1000);
  Serial.println(F("Arduino Mega started"));

  setupI2c();

  // OLED 컨트롤러를 초기화합니다.
  oled.setI2CAddress(oledI2cAddress << 1);
  // U8g2 기본값(400kHz) 대신 HuskyLens와 같은 100kHz로 고정합니다.
  oled.setBusClock(I2C_CLOCK);
  oled.begin();
  // oled.begin()이 내부에서 Wire.begin()을 다시 부르므로 타임아웃을 재설정합니다.
  Wire.setWireTimeout(25000, true);
  Serial.println(oledI2cDetected ? F("OLED initialized")
                                 : F("OLED init attempted; I2C device not detected"));
  showMessage(F("ROBOT START"), F("CONNECTING..."));

  Serial.println(F("Starting HuskyLens over I2C..."));
  for (int attempt = 0; attempt < 5 && !initHuskylens(); ++attempt) {
    Serial.println(F("HuskyLens I2C connection failed"));
    delay(500);
  }
  if (huskylensReady) {
    Serial.println(F("HuskyLens I2C connected"));
  } else {
    // 여기서 멈추지 않고 MQTT 원격 명령은 계속 받을 수 있게 합니다.
    // loop()에서 3초마다 다시 초기화를 시도합니다.
    Serial.println(F("HuskyLens unavailable. Check I2C wiring and set Protocol Type to I2C."));
    showMessage(F("HUSKYLENS ERROR"), F("CHECK I2C WIRING"));
  }

  setupNetwork();

  if (huskylensReady) showMessage(F("READY"), F("SHOW QR CODE"));
  Serial.print(F("Free RAM: "));
  extern int __heap_start, *__brkval;
  int v;
  Serial.println((int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval));
  Serial.println(F("Waiting for QR..."));
}

void loop() {
  unsigned long now = millis();
  // 현재 상태에 해당하는 코드만 실행합니다.
  switch (sequenceState) {
    case 0: { // QR 또는 MQTT 명령을 기다리는 대기 상태
      serviceBackground();
      maintainHuskylens();
      // MQTT 콜백에서 작업이 시작되었을 수 있습니다.
      if (sequenceState != 0) break;
      int room = readQrRoom();
      if (room >= 0) {
        // The Node-RED button selects whether this QR scan retrieves or loads.
        if (qrPutMode && roomLoaded[room]) {
          reportRoomStateConflict(room, true);
          delay(500);
          break;
        }
        if (!qrPutMode && !roomLoaded[room]) {
          reportRoomStateConflict(room, false);
          delay(500);
          break;
        }
        mqttPutMode = qrPutMode;
        Serial.print(F("QR detected: room ")); Serial.print(room + 1);
        Serial.print(F(", action: "));
        Serial.println(mqttPutMode ? F("LOAD") : F("RETRIEVE"));
        showMessage(F("QR ROOM DETECTED"),
                    mqttPutMode ? F("ACTION: LOAD") : F("ACTION: RETRIEVE"));
        publishStatus(mqttPutMode ? "qr_detected_load" : "qr_detected_retrieve");
        delay(500);
        startRoom(room);
      }
      break;
    }
    case 1: { // Run the current A-H action with the acceleration profile.
      if (activeMotor == nullptr) {
        Serial.println(F("Motion sequence state is invalid; stopping sequence"));
        sequenceState = 0;
        break;
      }
      // 모터가 도는 동안에는 스텝 펄스만 처리합니다.
      if (!runStepper(*activeMotor)) break;

      // 한 동작이 끝나 모터가 정지한 순간: 네트워크/I2C 처리를 여기서 합니다.
      serviceBackground();
      maintainHuskylens();

      MotionAction action = activeSequence[activeActionIndex];
      if ((action == ACTION_F || action == ACTION_G) && activeActionPhase == 0) {
        activeActionPhase = 1;
        startMotionAction(action, activeActionPhase);
        break;
      }
      activeActionPhase = 0;
      ++activeActionIndex;
      if (activeActionIndex < activeSequenceLength) {
        startMotionAction(activeSequence[activeActionIndex], activeActionPhase);
        break;
      }

      if (mqttPutMode) {
        roomLoaded[selectedRoom] = true;
        publishItemStatus("loaded");
        showMessage(F("PUT COMPLETE"), F("ITEM LOADED"));
      } else {
        roomLoaded[selectedRoom] = false;
        publishItemStatus("unloaded");
        showMessage(F("GET COMPLETE"), F("ITEM REMOVED"));
      }
      Serial.print(F("Motion sequence complete for room "));
      Serial.println(selectedRoom + 1);
      activeSequence = nullptr;
      activeSequenceLength = 0;
      activeMotor = nullptr;
      waitStartTime = millis();
      sequenceState = 9;
      break;
    }
    case 9: // Wait briefly, then accept the next command.
      serviceBackground();
      if (now - waitStartTime >= 1000) {
        // 완료 직후 같은 QR을 반복 인식하는 것을 방지하기 위해 잠시 대기합니다.
        Serial.println(F("Waiting for QR..."));
        showMessage(F("READY"), F("SHOW QR CODE"));
        sequenceState = 0;
      }
      break;
  }
}

void publishItemStatus(const char *status) {
  // MQTT room commands do not have a QR item ID; still publish room occupancy
  // so Node-RED can refresh its floor/storage widgets after the motion ends.
  if (!mqttClient.connected() || selectedRoom < 0) return;
  char message[64];
  snprintf_P(message, sizeof(message), PSTR("item_id=%d,room=%d,status=%s"),
             currentItemId, selectedRoom + 1, status);
  mqttClient.publish(MQTT_STATUS_TOPIC, message, true);
}
