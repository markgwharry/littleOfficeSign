# ESP32 Firmware Setup

## Required Libraries
Install via Arduino Library Manager:
- Adafruit_ILI9341
- Adafruit_GFX
- XPT2046_Touchscreen
- PubSubClient
- ArduinoJson

## Steps
1. Select **ESP32 Dev Module** board.
2. Set upload speed to **115200**.
3. Wire the display and touch per README wiring table.
4. Update Wi-Fi and MQTT credentials in the sketch.
5. Upload and verify. The sign should boot with a color flash and show FREE/BUSY.
6. Touch RING to publish to `office/sign/ring`.

### MQTT
- Sub: `office/sign/state`
- Pub: `office/sign/ring`

### Payload
```json
{
  "status":"busy",
  "now":{"title":"Team Meeting","end_local":"15:00"},
  "next":{"title":"1:1","start_local":"15:30"}
}
```
