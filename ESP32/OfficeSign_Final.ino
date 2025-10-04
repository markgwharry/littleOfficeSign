/*  (same as in chat; confirmed-good final sketch)  */
// Full sketch content follows:
/*
  Office Door Sign — ESP32 + ILI9341 + XPT2046
  ------------------------------------------------
  TFT (ILI9341) on HSPI:
    SCLK=14, MISO=12, MOSI=13, CS=15, DC=2, RST=-1, BL=21 (active-high)
  Touch (XPT2046) on VSPI:
    SCLK=25, MISO=39, MOSI=32, CS=27, IRQ unused (polling)

  MQTT
    Sub: office/sign/state   (expects JSON: {status, now{title,end(_local)}, next{title,start(_local)}})
    Pub: office/sign/ring    (publishes JSON on button press)

  Touch calibration (your panel):
    X: 180..3700,  Y: 250..3800

  Set your Wi-Fi + MQTT broker below.
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// ----------- Pins (TFT on HSPI) -----------
#define TFT_CS     15
#define TFT_DC      2
#define TFT_RST    -1
#define TFT_BL     21
#define HSPI_SCLK  14
#define HSPI_MISO  12
#define HSPI_MOSI  13

// ----------- Pins (Touch on VSPI) --------
#define T_CS       27
#define T_IRQ     255           // no IRQ; polling
#define T_SCLK     25
#define T_MISO     39
#define T_MOSI     32

// ----------- Wi-Fi + MQTT ----------------
const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* MQTT_HOST = "192.168.1.50";   // your NAS/broker IP
const uint16_t MQTT_PORT = 1883;
const char* SUB_TOPIC = "office/sign/state";
const char* PUB_RING  = "office/sign/ring";

// ----------- Globals ---------------------
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(T_CS); // polling

WiFiClient wifi;
PubSubClient mqtt(wifi);

// Touch calibration
int16_t rx_min = 180, rx_max = 3700, ry_min = 250, ry_max = 3800;

// UI state
bool isBusy = false;
String nowTitle = "";
String nextTitle = "";

// Button
struct Btn { int x,y,w,h; };
Btn ringBtn{10, 180, 300, 50};
bool inBtn(int x,int y,const Btn& b){ return x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h; }

// ------------- UI ------------------------
void drawUI() {
  uint16_t bg = isBusy ? ILI9341_RED : ILI9341_GREEN;
  tft.fillScreen(bg);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_BLACK, bg);

  tft.setCursor(10, 10);
  tft.print(isBusy ? "BUSY" : "FREE");

  tft.setCursor(10, 50);
  tft.print("Now:");
  tft.setCursor(10, 70);
  tft.print(nowTitle.length() ? nowTitle : (isBusy ? "(In meeting)" : "(Idle)"));

  tft.setCursor(10, 100);
  tft.print("Next:");
  tft.setCursor(10, 120);
  tft.print(nextTitle.length()? nextTitle : "(None)");

  // RING button
  tft.fillRoundRect(ringBtn.x, ringBtn.y, ringBtn.w, ringBtn.h, 8, ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(ringBtn.x + 90, ringBtn.y + 18);
  tft.print("RING");
}

// ------------- MQTT ----------------------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) return;

  const char* st = doc["status"] | "free";
  isBusy = (strcmp(st, "busy") == 0);

  String nowT  = String(doc["now"]["title"]  | "");
  String nextT = String(doc["next"]["title"] | "");

  // Optional local time strings provided by the bridge
  String nowEndLocal     = String(doc["now"]["end_local"]   | "");
  String nextStartLocal  = String(doc["next"]["start_local"]| "");

  if (nowEndLocal.length())     nowT  += " (" + nowEndLocal + ")";
  if (nextStartLocal.length())  nextT += " (" + nextStartLocal + ")";

  nowTitle  = nowT;
  nextTitle = nextT;

  drawUI();
}

void ensureMqtt() {
  while (!mqtt.connected()) {
    String cid = "office-sign-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqtt.connect(cid.c_str())) {
      mqtt.subscribe(SUB_TOPIC);
    } else {
      delay(1000);
    }
  }
}

// ------------- Setup & Loop --------------
void setup() {
  Serial.begin(115200);
  delay(50);

  // Backlight full on (active-high)
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Bring up TFT on HSPI
  SPI.end();
  SPI.begin(HSPI_SCLK, HSPI_MISO, HSPI_MOSI);
  tft.begin();
  tft.setRotation(1);

  // Quick color flash
  tft.fillScreen(ILI9341_RED);   delay(120);
  tft.fillScreen(ILI9341_GREEN); delay(120);
  tft.fillScreen(ILI9341_BLUE);  delay(120);

  drawUI();

  // Touch on VSPI
  touchSPI.begin(T_SCLK, T_MISO, T_MOSI, T_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250);
  }

  // MQTT
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) ensureMqtt();
    mqtt.loop();
  }

  // Touch handling (poll)
  if (touch.touched()) {
    TS_Point p = touch.getPoint(); // raw
    if (p.z > 10) {
      int16_t tx = map(p.x, rx_min, rx_max, 0, tft.width());
      int16_t ty = map(p.y, ry_min, ry_max, 0, tft.height());

      if (inBtn(tx, ty, ringBtn)) {
        // Visual feedback
        tft.drawRoundRect(ringBtn.x, ringBtn.y, ringBtn.w, ringBtn.h, 8, ILI9341_WHITE);
        delay(120);
        tft.drawRoundRect(ringBtn.x, ringBtn.y, ringBtn.w, ringBtn.h, 8, ILI9341_BLACK);

        // Publish ring event
        StaticJsonDocument<128> doc;
        doc["when"] = millis();
        char buf[128];
        size_t n = serializeJson(doc, buf, sizeof(buf));
        mqtt.publish(PUB_RING, buf, n);
        Serial.println("RING pressed -> MQTT published");
      }
    }
  }

  delay(10);
}
