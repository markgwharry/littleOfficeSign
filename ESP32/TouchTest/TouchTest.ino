/*
  Minimal Touch Test for CYD
  Tests XPT2046 touch separately from display
*/

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// Touch pins - YOUR board uses GPIO 27 for CS
#define T_CS    27
#define T_SCLK  25
#define T_MISO  39
#define T_MOSI  32

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(T_CS);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== Touch Test ===");
  Serial.printf("Pins: CS=%d, SCLK=%d, MISO=%d, MOSI=%d\n", T_CS, T_SCLK, T_MISO, T_MOSI);

  // Initialize touch
  pinMode(T_CS, OUTPUT);
  digitalWrite(T_CS, HIGH);

  touchSPI.begin(T_SCLK, T_MISO, T_MOSI, T_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  Serial.println("Touch initialized. Try touching the screen...");
}

void loop() {
  static uint32_t lastPrint = 0;

  bool touched = touch.touched();

  // Print status every second
  if (millis() - lastPrint > 1000) {
    Serial.printf("touched=%d\n", touched);
    lastPrint = millis();
  }

  if (touched) {
    TS_Point p = touch.getPoint();
    Serial.printf("TOUCH! x=%d y=%d z=%d\n", p.x, p.y, p.z);
    delay(50);  // Debounce
  }

  delay(10);
}
