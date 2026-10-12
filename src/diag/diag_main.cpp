#include <Arduino.h>
#include <Wire.h>

/*
 * ================================================================
 *  하드웨어 통신 진단 펌웨어 (Arduino Mega 2560)
 * ================================================================
 *
 *  본 프로그램 없이 배선/설정만 단계별로 검사합니다.
 *  업로드: PlatformIO 환경 "diagnose" 선택 후 Upload & Monitor
 *         (pio run -e diagnose -t upload -t monitor)
 *
 *  1. I2C 라인 전압 검사 (SDA=D20, SCL=D21)
 *  2. I2C 주소 스캔 (OLED 0x3C/0x3D, HuskyLens 0x32)
 *  3. HuskyLens I2C knock 명령을 직접 보내고 원시 응답 출력
 *  4. ESP-01을 여러 baud로 "AT" 호출 → 응답/펌웨어 버전 확인
 *     WiFiEspAT가 요구하는 AT+CIPRECVMODE 지원 여부 확인
 *  5. 이후 시리얼 모니터 ↔ ESP-01 직통 모드 (AT 명령 직접 입력 가능)
 *     시리얼 모니터에서 'r' 한 글자만 보내면 진단을 다시 실행합니다.
 */

const uint8_t SDA_PIN = 20;
const uint8_t SCL_PIN = 21;
const uint8_t HUSKYLENS_ADDR = 0x32;
const unsigned long BAUDS[] = {115200UL, 9600UL, 57600UL, 38400UL, 19200UL, 74880UL};

unsigned long espBaud = 0;
bool espLineStart = true;

void printHex(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

// ---------------------------------------------------------------
// I2C
// ---------------------------------------------------------------

void checkI2cLines() {
  Serial.println(F("\n[1] I2C 라인 전압 검사 (장치 연결된 상태, 통신 없음)"));
  pinMode(SDA_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);
  delay(5);
  bool sda = digitalRead(SDA_PIN);
  bool scl = digitalRead(SCL_PIN);
  Serial.print(F("    SDA(D20)=")); Serial.print(sda ? F("HIGH") : F("LOW"));
  Serial.print(F("  SCL(D21)=")); Serial.println(scl ? F("HIGH") : F("LOW"));
  if (sda && scl) {
    Serial.println(F("    OK: 두 라인 모두 정상적으로 HIGH 입니다."));
  } else {
    Serial.println(F("    FAIL: 라인이 LOW로 붙어 있습니다. I2C 전체가 동작할 수 없습니다."));
    Serial.println(F("      - 전원이 꺼진 장치가 SDA/SCL에 연결되어 있지 않은지"));
    Serial.println(F("      - SDA/SCL 선이 GND에 닿거나 서로 바뀌지 않았는지"));
    Serial.println(F("      - 장치를 하나씩 뽑아 가며 어느 장치가 원인인지 확인"));
  }
}

bool scanI2c(bool &oled, bool &husky) {
  Serial.println(F("\n[2] I2C 주소 스캔 (100kHz)"));
  Wire.begin();
  Wire.setClock(100000);
  Wire.setWireTimeout(25000, true);
  oled = husky = false;
  uint8_t count = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 5) {
      Serial.println(F("    FAIL: I2C 타임아웃 - 버스가 응답하지 않습니다 (라인 LOW 고정 의심)"));
      return false;
    }
    if (err == 0) {
      ++count;
      Serial.print(F("    found 0x")); printHex(addr);
      if (addr == 0x3C || addr == 0x3D) { oled = true; Serial.print(F("  <- OLED")); }
      if (addr == HUSKYLENS_ADDR) { husky = true; Serial.print(F("  <- HuskyLens")); }
      Serial.println();
    }
  }
  if (count == 0) {
    Serial.println(F("    FAIL: 아무 장치도 없습니다."));
    Serial.println(F("      - 배선이 Mega의 SDA=D20, SCL=D21 인지 (ESP32의 21/22 아님)"));
    Serial.println(F("      - 각 장치 VCC/GND, 그리고 GND가 Mega와 공통인지"));
  }
  Serial.print(F("    OLED      : ")); Serial.println(oled ? F("OK") : F("없음"));
  Serial.print(F("    HuskyLens : ")); Serial.println(husky ? F("OK (0x32 ACK)") : F("없음"));
  if (!husky) {
    Serial.println(F("      HuskyLens 확인 사항:"));
    Serial.println(F("      - 설정 > Protocol Type 을 'I2C' 로 바꾸고 저장(나갈 때 저장 선택)"));
    Serial.println(F("      - 4핀 케이블: T(초록)=SDA→D20, R(파랑)=SCL→D21, +(빨강)=5V, -(검정)=GND"));
    Serial.println(F("      - HuskyLens 화면이 켜져 있는지 (USB 전원 없이 Mega 5V만으로는 부족할 수 있음)"));
  }
  return count > 0;
}

void testHuskylensRaw() {
  Serial.println(F("\n[3] HuskyLens knock 명령 직접 전송"));
  // Protocol: 55 AA 11 len=00 cmd=2C(knock) checksum=3C
  const uint8_t knock[] = {0x55, 0xAA, 0x11, 0x00, 0x2C, 0x3C};
  Wire.beginTransmission(HUSKYLENS_ADDR);
  Wire.write(knock, sizeof(knock));
  uint8_t err = Wire.endTransmission();
  Serial.print(F("    write result = ")); Serial.print(err);
  Serial.println(err == 0 ? F(" (ACK)") : F(" (NACK/에러: 장치가 응답하지 않음)"));
  if (err != 0) return;
  delay(20);
  uint8_t n = Wire.requestFrom(HUSKYLENS_ADDR, (uint8_t)16);
  Serial.print(F("    read ")); Serial.print(n); Serial.print(F(" bytes: "));
  bool okFrame = false;
  uint8_t prev = 0;
  while (Wire.available()) {
    uint8_t b = Wire.read();
    printHex(b); Serial.print(' ');
    if (prev == 0x55 && b == 0xAA) okFrame = true;
    prev = b;
  }
  Serial.println();
  Serial.println(okFrame ? F("    OK: HuskyLens가 프로토콜 응답(55 AA ...)을 보냈습니다.")
                         : F("    FAIL: 응답 형식이 아닙니다. Protocol Type=I2C 설정을 확인하세요."));
}

// ---------------------------------------------------------------
// ESP-01
// ---------------------------------------------------------------

// ESP-01로 명령을 보내고 timeout 동안 받은 응답을 그대로 출력합니다.
// 응답 안에 "OK"가 있으면 true.
bool espCommand(const __FlashStringHelper *cmd, unsigned long timeout, bool echo = true) {
  while (Serial2.available()) Serial2.read();
  Serial2.print(cmd);
  Serial2.print(F("\r\n"));
  unsigned long start = millis();
  char last2[2] = {0, 0};
  bool ok = false;
  bool lineStart = true;
  while (millis() - start < timeout) {
    if (!Serial2.available()) continue;
    char c = Serial2.read();
    if (echo) {
      if (lineStart) { Serial.print(F("      < ")); lineStart = false; }
      if (c >= 0x20 || c == '\n') Serial.write(c);
      else if (c != '\r') { Serial.print(F("\\x")); printHex((uint8_t)c); }
      if (c == '\n') lineStart = true;
    }
    if (last2[1] == 'O' && c == 'K') ok = true;
    last2[0] = last2[1];
    last2[1] = c;
  }
  if (echo && !lineStart) Serial.println();
  return ok;
}

void testEsp() {
  Serial.println(F("\n[4] ESP-01 AT 응답 검사 (Mega TX2=D16 → ESP RX, Mega RX2=D17 ← ESP TX)"));
  espBaud = 0;
  for (uint8_t i = 0; i < sizeof(BAUDS) / sizeof(BAUDS[0]); ++i) {
    Serial2.end();
    Serial2.begin(BAUDS[i]);
    delay(50);
    Serial.print(F("    baud ")); Serial.print(BAUDS[i]); Serial.println(F(" : AT"));
    if (espCommand(F("AT"), 600)) {
      espBaud = BAUDS[i];
      Serial.println(F("      -> OK 응답!"));
      break;
    }
  }
  if (espBaud == 0) {
    Serial.println(F("    FAIL: 어떤 속도에서도 OK 응답이 없습니다."));
    Serial.println(F("      - TX/RX 교차 연결: Mega D16(TX2)→ESP RX, Mega D17(RX2)←ESP TX"));
    Serial.println(F("        (D18/D19는 Serial1 입니다. 핀 번호를 다시 확인하세요)"));
    Serial.println(F("      - ESP-01 전원: 3.3V, 300mA 이상. Mega 3.3V 핀(50mA)으로는 부족"));
    Serial.println(F("      - CH_PD(EN)과 RST 핀을 3.3V에 연결 (EN이 떠 있으면 칩이 꺼져 있음)"));
    Serial.println(F("      - GND 공통, ESP 파란 LED가 전원 인가 시 잠깐 깜빡이는지"));
    Serial.println(F("      - 위 응답 줄에 깨진 문자만 보이면 baud가 다른 것이고,"));
    Serial.println(F("        아무것도 없으면 배선/전원 문제입니다."));
    Serial.println(F("      - AT 펌웨어가 아닌 다른 펌웨어(NodeMCU 등)가 올라가 있을 수 있음"));
    return;
  }

  Serial.println(F("    AT+GMR (펌웨어 버전):"));
  espCommand(F("AT+GMR"), 1500);
  Serial.println(F("    AT+CIPRECVMODE? (WiFiEspAT 필수, AT 1.7 이상):"));
  bool recvMode = espCommand(F("AT+CIPRECVMODE?"), 1000);
  if (!recvMode) {
    Serial.println(F("    FAIL: 이 ESP-01의 AT 펌웨어가 너무 오래되었습니다."));
    Serial.println(F("      WiFiEspAT는 AT 1.7 이상(또는 2.x)이 필요해서 WiFi.init()이 실패합니다."));
    Serial.println(F("      ESP-01을 AT 1.7.x 펌웨어로 업데이트하세요 (ESP8266 NONOS AT 1.7.4/1.7.5)."));
  } else {
    Serial.println(F("    OK: WiFiEspAT와 호환되는 펌웨어입니다."));
  }
  if (espBaud != 115200UL) {
    Serial.print(F("    NOTE: ESP-01이 ")); Serial.print(espBaud);
    Serial.println(F(" baud 입니다. main.cpp의 ESP_AT_BAUD 값을 이 값으로 바꾸세요."));
    if (espBaud == 9600UL)
      Serial.println(F("      (9600은 느리지만 Mega에서 가장 안정적입니다. 그대로 쓰셔도 됩니다)"));
  }
}

void runDiagnostics() {
  Serial.println(F("\n================ HARDWARE DIAGNOSTICS ================"));
  checkI2cLines();
  bool oled, husky;
  if (scanI2c(oled, husky) && husky) testHuskylensRaw();
  testEsp();
  Serial.println(F("\n[5] 직통 모드: 시리얼 모니터에 입력한 내용이 ESP-01로 전달됩니다."));
  Serial.println(F("    (줄 끝 CRLF 설정 권장, 'r' 한 글자 입력 시 진단 재실행)"));
  Serial.println(F("======================================================"));
  if (espBaud == 0) {
    Serial2.end();
    Serial2.begin(115200UL);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  runDiagnostics();
}

void loop() {
  static char pending = 0;
  static unsigned long pendingAt = 0;
  // 시리얼 모니터 → ESP-01. 단독 'r' 입력은 재진단 명령으로 처리합니다.
  while (Serial.available()) {
    char c = Serial.read();
    if (pending == 'r' && (c == '\r' || c == '\n')) {
      pending = 0;
      while (Serial.available()) Serial.read();
      runDiagnostics();
      return;
    }
    if (pending) { Serial2.write(pending); pending = 0; }
    if (c == 'r' && espLineStart) { pending = c; pendingAt = millis(); espLineStart = false; continue; }
    Serial2.write(c);
    espLineStart = (c == '\n');
  }
  if (pending && millis() - pendingAt > 300) {
    // 줄바꿈 없이 'r'만 보낸 경우도 재진단
    pending = 0;
    espLineStart = true;
    runDiagnostics();
    return;
  }
  // ESP-01 → 시리얼 모니터
  while (Serial2.available()) Serial.write(Serial2.read());
}
