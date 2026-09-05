
# OverTake

A retro 8-bit endless motorcycle racing game running on an ESP32 microcontroller with an SSD1306 I2C OLED display[span_0](start_span)[span_0](end_span). The game creates its own local Wi-Fi Access Point, serving an interactive touch gamepad directly to any smartphone browser for real-time steering[span_1](start_span)[span_1](end_span).

---
Link= "http://192.168.4.1"
---

## Hardware Requirements

* ESP32 Development Board (NodeMCU ESP32, ESP32-WROOM-32, etc.)[span_2](start_span)[span_2](end_span)
* 0.96" or 1.3" I2C SSD1306 OLED Display (128x64 resolution)[span_3](start_span)[span_3](end_span)
* 4-pin Jumper Wires
* Breadboard (optional)
* Any smartphone with Wi-Fi and a modern web browser[span_4](start_span)[span_4](end_span)

---

## Wiring & Connections

Connect the SSD1306 OLED display to the ESP32 using the default I2C pins defined in the code[span_5](start_span)[span_5](end_span):

| SSD1306 OLED Pin | ESP32 GPIO Pin | Description |
| :--- | :--- | :--- |
| **VCC** | **3.3V** or **VIN** | Display Power (check module voltage rating: 3.3V–5V) |
| **GND** | **GND** | Ground |
| **SDA** | **GPIO 21** | I2C Data line (`Wire.begin(21, 22)`)[span_6](start_span)[span_6](end_span) |
| **SCK / SCL** | **GPIO 22** | I2C Clock line (`Wire.begin(21, 22)`)[span_7](start_span)[span_7](end_span) |

* Note: The sketch initializes I2C communication at standard address `0x3C` using Fast Mode (400 kHz)[span_8](start_span)[span_8](end_span).

---

## Software & Required Libraries

Install the following libraries via the Arduino IDE Library Manager (**Sketch** > **Include Library** > **Manage Libraries...**):

* **Adafruit SSD1306** by Adafruit[span_9](start_span)[span_9](end_span)
* **Adafruit GFX Library** by Adafruit[span_10](start_span)[span_10](end_span)
* **WiFi** (bundled with ESP32 board package)[span_11](start_span)[span_11](end_span)
* **WebServer** (bundled with ESP32 board package)[span_12](start_span)[span_12](end_span)
* **Wire** (bundled with ESP32 board package)[span_13](start_span)[span_13](end_span)

---

## How to Play

### 1. Flash the ESP32
* Open the `.ino` sketch in Arduino IDE or VS Code PlatformIO[span_14](start_span)[span_14](end_span).
* Select your ESP32 board and COM port.
* Compile and upload the sketch[span_15](start_span)[span_15](end_span).

### 2. Connect to the Game's Wi-Fi Network
* On your phone, go to **Settings > Wi-Fi** and look for available networks.
* **Network Name (SSID):** `0`[span_16](start_span)[span_16](end_span)
* **Password:** `12345678`[span_17](start_span)[span_17](end_span)
* Connect to this network (if prompted that internet is not available, tap *Keep Wi-Fi connection*).

### 3. Open the Controller Link
* Open any mobile browser (Safari, Chrome, Firefox) on your connected phone[span_18](start_span)[span_18](end_span).
* Navigate to the default ESP32 gateway URL:
  ```text
  [http://192.168.4.1](http://192.168.4.1)

 * The embedded web controller will load full-screen touch directional buttons (◀ and ▶).
4. Game Controls & Rules
 * Steer Left / Right: Tap the left (◀) or right (▶) buttons on your phone to switch lanes.
 * Avoid Traffic: Dodge oncoming cars moving along the 3 highway lanes.
 * Speed & Score: The bike's speed progressively accelerates over time, increasing your score multiplier.
 * Restarting after Crash: When you collide with another car, CRASHED! will appear on the OLED display. Tap either arrow button on your phone controller to instantly reset and restart the game.

