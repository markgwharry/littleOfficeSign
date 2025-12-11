/*
  Office Door Sign v2.1 — ESP32 CYD
  ==================================
  Enhanced UI with:
  - Large FREE/BUSY status (4-year-old friendly!)
  - Dark theme with bright status colors
  - Custom icons (WiFi, MQTT, Bell)
  - Animated bell icon when ringing
  - Status bar with WiFi/MQTT indicators + clock
  - Settings screen via swipe-down gesture
  - Brightness control

  Hardware:
  - TFT (ILI9341) on HSPI: SCLK=14, MISO=12, MOSI=13, CS=15, DC=2, BL=21
  - Touch (XPT2046) on VSPI: SCLK=25, MISO=39, MOSI=32, CS=27
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <time.h>

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
#define T_SCLK     25
#define T_MISO     39
#define T_MOSI     32

// ----------- Wi-Fi + MQTT ----------------
const char* WIFI_SSID = "Mark & Kiwi IoT";
const char* WIFI_PASS = "49Fmn5p5";
const char* MQTT_HOST = "192.168.1.125";
const uint16_t MQTT_PORT = 1883;
const char* SUB_TOPIC = "office/sign/state";
const char* PUB_RING  = "office/sign/ring";

// NTP
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET = 0;
const int DST_OFFSET = 3600;

// ----------- Colors (RGB565) -------------
#define COLOR_BG        0x1082  // Dark gray
#define COLOR_BAR       0x2104  // Status bar gray
#define COLOR_GREEN     0x07E0  // Bright green
#define COLOR_RED       0xF800  // Bright red
#define COLOR_CYAN      0x07FF  // Cyan for MQTT
#define COLOR_WHITE     0xFFFF
#define COLOR_GRAY      0x8410
#define COLOR_BTN       0x3186  // Button gray
#define COLOR_YELLOW    0xFFE0  // Yellow for bell
#define COLOR_ORANGE    0xFD20  // Orange for bell accent

// ----------- Layout ----------------------
#define STATUS_BAR_H  24
#define STATUS_Y      28
#define STATUS_H      100
#define BTN_X         10
#define BTN_Y         190
#define BTN_W         300
#define BTN_H         44

// ----------- Globals ---------------------
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(T_CS);

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences prefs;

// Touch calibration
int16_t rx_min = 180, rx_max = 3700, ry_min = 250, ry_max = 3800;

// State
bool isBusy = false;
String nowTitle = "";
String nowEnd = "";
String nextTitle = "";
String nextStart = "";
bool wifiConnected = false;
bool mqttConnected = false;

// Touch tracking for swipe
bool touchActive = false;
int16_t touchStartX, touchStartY;
int16_t touchLastX, touchLastY;
uint32_t touchStartTime;

// Settings
bool inSettings = false;
uint8_t brightness = 255;
uint32_t lastClockUpdate = 0;

// ----------- Helpers ---------------------
bool inButton(int x, int y) {
  return x >= BTN_X && x < BTN_X + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

String getTimeStr() {
  struct tm t;
  if (!getLocalTime(&t, 100)) return "--:--";
  char buf[6];
  strftime(buf, 6, "%H:%M", &t);
  return String(buf);
}

void flashBacklight() {
  for (int i = 0; i < 3; i++) {
    ledcWrite(TFT_BL, 50);
    delay(50);
    ledcWrite(TFT_BL, brightness);
    delay(50);
  }
}

// ----------- Icon Drawing ----------------
// Draw WiFi icon (3 arcs)
void drawWifiIcon(int x, int y, uint16_t color) {
  // Base dot
  tft.fillCircle(x, y + 10, 2, color);
  // Arcs (drawn as partial circles using lines)
  for (int r = 4; r <= 10; r += 3) {
    for (int a = -45; a <= 45; a += 5) {
      float rad = a * 3.14159 / 180.0;
      int px = x + (int)(sin(rad) * r);
      int py = y + 10 - (int)(cos(rad) * r);
      tft.drawPixel(px, py, color);
      tft.drawPixel(px + 1, py, color);
    }
  }
}

// Draw cloud/server icon for MQTT
void drawMqttIcon(int x, int y, uint16_t color) {
  // Simple server/box with signal
  tft.fillRoundRect(x - 5, y + 4, 10, 8, 2, color);
  tft.drawLine(x - 3, y + 7, x + 2, y + 7, COLOR_BAR);
  tft.drawLine(x - 3, y + 9, x + 2, y + 9, COLOR_BAR);
  // Signal dot above
  tft.fillCircle(x, y + 1, 1, color);
}

// Draw bell icon at position with optional tilt for animation
void drawBell(int cx, int cy, uint16_t color, int tilt = 0) {
  // Bell body (using triangles and circles for shape)
  // tilt: -1 = left, 0 = center, 1 = right
  int offsetX = tilt * 3;

  // Clear previous bell area
  tft.fillRect(cx - 14, cy - 16, 28, 34, COLOR_BTN);

  // Bell dome (top part) - filled circle
  tft.fillCircle(cx + offsetX, cy - 4, 10, color);

  // Bell body (bottom trapezoid shape)
  tft.fillTriangle(
    cx + offsetX - 10, cy - 4,
    cx + offsetX + 10, cy - 4,
    cx + offsetX - 13, cy + 8,
    color
  );
  tft.fillTriangle(
    cx + offsetX + 10, cy - 4,
    cx + offsetX - 13, cy + 8,
    cx + offsetX + 13, cy + 8,
    color
  );

  // Bell rim (bottom)
  tft.fillRoundRect(cx + offsetX - 14, cy + 6, 28, 5, 2, color);

  // Clapper (little ball at bottom)
  tft.fillCircle(cx + offsetX, cy + 14, 3, COLOR_ORANGE);

  // Handle at top
  tft.fillCircle(cx + offsetX, cy - 13, 3, color);
  tft.fillRect(cx + offsetX - 1, cy - 14, 2, 3, color);
}

// Draw a LARGE bell for the ring animation (fills most of screen)
void drawBigBell(int cx, int cy, int tilt, uint16_t bgColor) {
  int offsetX = tilt * 15;  // Bigger swing for big bell

  // Clear area
  tft.fillRect(cx - 70, cy - 80, 140, 170, bgColor);

  // Bell dome (top part) - large filled circle
  tft.fillCircle(cx + offsetX, cy - 20, 50, COLOR_YELLOW);

  // Bell body (bottom trapezoid shape)
  tft.fillTriangle(
    cx + offsetX - 50, cy - 20,
    cx + offsetX + 50, cy - 20,
    cx + offsetX - 65, cy + 40,
    COLOR_YELLOW
  );
  tft.fillTriangle(
    cx + offsetX + 50, cy - 20,
    cx + offsetX - 65, cy + 40,
    cx + offsetX + 65, cy + 40,
    COLOR_YELLOW
  );

  // Bell rim (bottom)
  tft.fillRoundRect(cx + offsetX - 68, cy + 35, 136, 18, 6, COLOR_YELLOW);

  // Clapper (ball at bottom)
  tft.fillCircle(cx + offsetX, cy + 65, 12, COLOR_ORANGE);

  // Handle at top
  tft.fillCircle(cx + offsetX, cy - 68, 12, COLOR_YELLOW);
  tft.fillRect(cx + offsetX - 4, cy - 72, 8, 10, COLOR_YELLOW);

  // Sound waves on alternating sides
  uint16_t waveColor = COLOR_WHITE;
  if (tilt < 0) {
    // Waves on right side
    for (int i = 0; i < 3; i++) {
      int wx = cx + 80 + i * 12;
      int wy = cy - 10;
      tft.drawLine(wx, wy - 15, wx + 8, wy - 25, waveColor);
      tft.drawLine(wx, wy, wx + 10, wy, waveColor);
      tft.drawLine(wx, wy + 15, wx + 8, wy + 25, waveColor);
    }
  } else if (tilt > 0) {
    // Waves on left side
    for (int i = 0; i < 3; i++) {
      int wx = cx - 80 - i * 12;
      int wy = cy - 10;
      tft.drawLine(wx, wy - 15, wx - 8, wy - 25, waveColor);
      tft.drawLine(wx, wy, wx - 10, wy, waveColor);
      tft.drawLine(wx, wy + 15, wx - 8, wy + 25, waveColor);
    }
  }
}

// Full-screen bell ring animation
void animateBellRing() {
  // Save current state - we'll do a full screen takeover
  tft.fillScreen(COLOR_BG);

  // Draw "RINGING!" text at top
  tft.setTextColor(COLOR_YELLOW);
  tft.setTextSize(3);
  tft.setCursor(80, 10);
  tft.print("RINGING!");

  int bellX = 160;  // Center of screen
  int bellY = 130;  // Center vertically

  // Big swinging animation - 8 swings
  for (int i = 0; i < 8; i++) {
    int tilt = (i % 2 == 0) ? -1 : 1;
    drawBigBell(bellX, bellY, tilt, COLOR_BG);
    delay(120);
  }

  // Final centered position
  drawBigBell(bellX, bellY, 0, COLOR_BG);
  delay(200);
}

// ----------- Drawing ---------------------
void drawStatusBar() {
  tft.fillRect(0, 0, 320, STATUS_BAR_H, COLOR_BAR);

  // WiFi icon
  drawWifiIcon(14, 2, wifiConnected ? COLOR_GREEN : COLOR_RED);

  // MQTT icon
  drawMqttIcon(38, 2, mqttConnected ? COLOR_CYAN : COLOR_RED);

  // Clock
  tft.setTextColor(COLOR_WHITE, COLOR_BAR);
  tft.setTextSize(2);
  tft.setCursor(250, 4);
  tft.print(getTimeStr());
}

void drawMainUI() {
  inSettings = false;
  tft.fillScreen(COLOR_BG);
  drawStatusBar();

  // Status area
  uint16_t statusColor = isBusy ? COLOR_RED : COLOR_GREEN;
  tft.fillRoundRect(8, STATUS_Y, 304, STATUS_H, 8, statusColor);

  // Status text - EXTRA LARGE for visibility
  const char* statusText = isBusy ? "BUSY" : "FREE";

  // Draw shadow first (offset by 2 pixels)
  tft.setTextColor(0x0000);  // Black shadow
  tft.setTextSize(5);  // Larger text size
  int16_t textX = (320 - strlen(statusText) * 30) / 2;
  tft.setCursor(textX + 2, STATUS_Y + 26 + 2);
  tft.print(statusText);

  // Draw main text
  tft.setTextColor(COLOR_WHITE);  // White text for contrast
  tft.setCursor(textX, STATUS_Y + 26);
  tft.print(statusText);

  // Meeting info
  tft.setTextSize(1);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.setCursor(10, 136);
  tft.print("NOW: ");
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  if (nowTitle.length() > 0) {
    String d = nowTitle;
    if (nowEnd.length() > 0) d += " (ends " + nowEnd + ")";
    if (d.length() > 48) d = d.substring(0, 45) + "...";
    tft.print(d);
  } else {
    tft.print(isBusy ? "(In meeting)" : "(Free)");
  }

  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.setCursor(10, 156);
  tft.print("NEXT: ");
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  if (nextTitle.length() > 0) {
    String d = nextTitle;
    if (nextStart.length() > 0) d += " (" + nextStart + ")";
    if (d.length() > 48) d = d.substring(0, 45) + "...";
    tft.print(d);
  } else {
    tft.print("(None)");
  }

  // Ring button with bell icon
  tft.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, COLOR_BTN);
  tft.drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, COLOR_GRAY);

  // Draw bell icon on button
  drawBell(BTN_X + 45, BTN_Y + 22, COLOR_YELLOW, 0);

  // Button text - shifted right to make room for bell
  tft.setTextColor(COLOR_WHITE, COLOR_BTN);
  tft.setTextSize(2);
  tft.setCursor(BTN_X + 80, BTN_Y + 14);
  tft.print("RING BELL");
}

void drawSettings() {
  inSettings = true;
  tft.fillScreen(COLOR_BG);

  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(100, 10);
  tft.print("SETTINGS");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.setCursor(10, 45);
  tft.print("WiFi: ");
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.print(WIFI_SSID);
  tft.print(wifiConnected ? " (OK)" : " (FAIL)");

  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.setCursor(10, 65);
  tft.print("MQTT: ");
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.print(MQTT_HOST);
  tft.print(mqttConnected ? " (OK)" : " (FAIL)");

  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.setCursor(10, 95);
  tft.print("Brightness: ");
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.print(brightness);

  // Brightness slider
  tft.drawRect(10, 115, 200, 20, COLOR_GRAY);
  int bw = map(brightness, 0, 255, 0, 196);
  tft.fillRect(12, 117, bw, 16, COLOR_GREEN);

  // Done button
  tft.fillRoundRect(100, 190, 120, 40, 6, COLOR_BTN);
  tft.setTextColor(COLOR_WHITE, COLOR_BTN);
  tft.setTextSize(2);
  tft.setCursor(130, 202);
  tft.print("DONE");

  tft.setTextSize(1);
  tft.setTextColor(COLOR_GRAY, COLOR_BG);
  tft.setCursor(60, 230);
  tft.print("Swipe up from bottom to close");
}

// ----------- MQTT ------------------------
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, payload, len)) return;

  const char* st = doc["status"] | "free";
  isBusy = (strcmp(st, "busy") == 0);

  nowTitle = String(doc["now"]["title"] | "");
  nowEnd = String(doc["now"]["end_local"] | "");
  nextTitle = String(doc["next"]["title"] | "");
  nextStart = String(doc["next"]["start_local"] | "");

  if (!inSettings) drawMainUI();
}

void ensureMqtt() {
  if (mqtt.connected()) return;
  String cid = "office-sign-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqtt.connect(cid.c_str())) {
    mqtt.subscribe(SUB_TOPIC);
    mqttConnected = true;
    if (!inSettings) drawStatusBar();
  }
}

void publishRing() {
  StaticJsonDocument<128> doc;
  doc["event"] = "ring";
  doc["when"] = millis();
  char buf[128];
  serializeJson(doc, buf);
  mqtt.publish(PUB_RING, buf);
  Serial.println("RING!");
}

// ----------- Touch -----------------------
void handleTouch() {
  bool touched = touch.touched();

  if (touched) {
    TS_Point p = touch.getPoint();
    if (p.z < 10) return;

    int16_t tx = map(p.x, rx_min, rx_max, 0, 320);
    int16_t ty = map(p.y, ry_min, ry_max, 0, 240);

    if (!touchActive) {
      touchActive = true;
      touchStartX = tx;
      touchStartY = ty;
      touchStartTime = millis();
    }
    touchLastX = tx;
    touchLastY = ty;

    // Settings touch handling
    if (inSettings) {
      // Brightness slider
      if (ty >= 115 && ty <= 135 && tx >= 10 && tx <= 210) {
        brightness = map(constrain(tx, 12, 208), 12, 208, 20, 255);
        ledcWrite(TFT_BL, brightness);
        tft.fillRect(12, 117, 196, 16, COLOR_BG);
        int bw = map(brightness, 0, 255, 0, 196);
        tft.fillRect(12, 117, bw, 16, COLOR_GREEN);
        tft.fillRect(80, 95, 50, 10, COLOR_BG);
        tft.setTextColor(COLOR_WHITE, COLOR_BG);
        tft.setTextSize(1);
        tft.setCursor(80, 95);
        tft.print(brightness);
      }
      return;
    }

    // Main screen - button press visual
    if (inButton(tx, ty)) {
      tft.drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, COLOR_WHITE);
    }

  } else {
    if (touchActive) {
      uint32_t dur = millis() - touchStartTime;
      int16_t swipeY = touchLastY - touchStartY;
      int16_t swipeX = abs(touchLastX - touchStartX);

      // Swipe down from top -> settings
      if (!inSettings && touchStartY < 30 && swipeY > 60 && swipeX < 80 && dur < 1000) {
        drawSettings();
        touchActive = false;
        return;
      }

      // Swipe up from bottom -> close settings
      if (inSettings && touchStartY > 190 && swipeY < -60 && swipeX < 80 && dur < 1000) {
        prefs.begin("sign", false);
        prefs.putUChar("bright", brightness);
        prefs.end();
        drawMainUI();
        touchActive = false;
        return;
      }

      // Done button in settings
      if (inSettings && touchLastX >= 100 && touchLastX < 220 && touchLastY >= 190 && touchLastY < 230 && dur < 500) {
        prefs.begin("sign", false);
        prefs.putUChar("bright", brightness);
        prefs.end();
        drawMainUI();
        touchActive = false;
        return;
      }

      // Ring button
      if (!inSettings && inButton(touchLastX, touchLastY) && dur < 500) {
        // Publish MQTT first (so notification goes out immediately)
        publishRing();

        // Full-screen bell animation takes over the display
        animateBellRing();

        // Flash backlight for extra feedback
        flashBacklight();

        // Redraw the main UI after animation completes
        drawMainUI();
      }

      touchActive = false;
    }
  }
}

// ----------- Setup & Loop ----------------
void setup() {
  Serial.begin(115200);
  delay(50);

  // Backlight on
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // TFT on HSPI
  SPI.end();
  SPI.begin(HSPI_SCLK, HSPI_MISO, HSPI_MOSI);
  tft.begin();
  tft.setRotation(1);

  // Color flash
  tft.fillScreen(ILI9341_RED);   delay(120);
  tft.fillScreen(ILI9341_GREEN); delay(120);
  tft.fillScreen(ILI9341_BLUE);  delay(120);

  // Boot screen
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(80, 100);
  tft.print("Office Sign");

  // Touch on VSPI
  touchSPI.begin(T_SCLK, T_MISO, T_MOSI, T_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  // Load settings
  prefs.begin("sign", true);
  brightness = prefs.getUChar("bright", 255);
  prefs.end();

  // Setup PWM for brightness AFTER touch init
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, brightness);

  // Wi-Fi
  tft.setTextSize(1);
  tft.setCursor(90, 130);
  tft.print("Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(250);
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  // NTP
  if (wifiConnected) {
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
  }

  // MQTT
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);

  delay(300);
  drawMainUI();
}

void loop() {
  // WiFi status
  bool wasConnected = wifiConnected;
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wasConnected != wifiConnected && !inSettings) drawStatusBar();

  // MQTT
  if (wifiConnected) {
    if (!mqtt.connected()) {
      if (mqttConnected) {
        mqttConnected = false;
        if (!inSettings) drawStatusBar();
      }
      ensureMqtt();
    }
    mqtt.loop();
  }

  // Clock update every minute
  if (!inSettings && millis() - lastClockUpdate > 60000) {
    drawStatusBar();
    lastClockUpdate = millis();
  }

  handleTouch();
  delay(20);
}
