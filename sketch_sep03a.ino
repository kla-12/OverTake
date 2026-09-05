#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Wi-Fi SoftAP Configuration ---
const char* apSSID = "0";
const char* apPass = "12345678";
WebServer server(80);

// --- Game Mechanics & Constants ---
const int LANE_CENTERS[3] = {41, 64, 87};
const int BIKE_W = 7;
const int BIKE_H = 11;
const int CAR_W  = 9;
const int CAR_H  = 13;

struct Car {
  float y;
  int lane;
  bool active;
};

const int MAX_CARS = 2; // Kept to 2 simultaneous cars for fair reaction on 64px height
Car cars[MAX_CARS];

int playerLane = 1;
float playerX = 64.0;
float targetX = 64.0;
const int playerY = 50;

float currentSpeed = 60.0;
unsigned long score = 0;
bool gameOver = false;
float roadScrollOffset = 0.0;
unsigned long lastSpeedRamp = 0;

// --- Embedded Web Controller ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <style>
    body { margin: 0; background: #0a0a0a; display: flex; height: 100vh; user-select: none; }
    .btn { flex: 1; margin: 12px; border-radius: 18px; background: #1f1f1f; color: #00ffcc;
           font-size: 5rem; display: flex; align-items: center; justify-content: center;
           border: 2px solid #333; touch-action: manipulation; }
    .btn:active { background: #00ffcc; color: #000; }
  </style>
</head>
<body>
  <div class="btn" onpointerdown="send('L')">&#9664;</div>
  <div class="btn" onpointerdown="send('R')">&#9654;</div>
  <script>
    function send(dir) {
      fetch('/cmd?dir=' + dir, { method: 'POST' });
      if (navigator.vibrate) navigator.vibrate(30);
    }
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleCommand() {
  if (server.hasArg("dir")) {
    String dir = server.arg("dir");
    if (gameOver) {
      resetGame();
    } else {
      if (dir == "L" && playerLane > 0) {
        playerLane--;
      } else if (dir == "R" && playerLane < 2) {
        playerLane++;
      }
      targetX = LANE_CENTERS[playerLane];
    }
  }
  server.send(200, "text/plain", "OK");
}

void resetGame() {
  playerLane = 1;
  playerX = 64.0;
  targetX = 64.0;
  currentSpeed = 60.0;
  score = 0;
  gameOver = false;

  cars[0].lane = 0;
  cars[0].y = -15;
  cars[0].active = true;

  cars[1].lane = 2;
  cars[1].y = -55;
  cars[1].active = true;
}

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  Wire.setClock(400000); // 400kHz Fast I2C

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  WiFi.softAP(apSSID, apPass);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/cmd", HTTP_POST, handleCommand);
  server.begin();

  resetGame();
}

void updatePhysics() {
  if (gameOver) return;

  // Smooth lane interpolation
  playerX += (targetX - playerX) * 0.40;

  // Progressive speed acceleration
  if (millis() - lastSpeedRamp > 1000 && currentSpeed < 220.0) {
    currentSpeed += 1.0;
    lastSpeedRamp = millis();
  }

  float dy = currentSpeed / 22.0;
  score += (unsigned long)(dy * 2);

  roadScrollOffset += dy;
  if (roadScrollOffset >= 16.0) roadScrollOffset = 0.0;

  // Update Traffic
  for (int i = 0; i < MAX_CARS; i++) {
    cars[i].y += dy * 0.55; // Traffic moves slower than player

    if (cars[i].y > SCREEN_HEIGHT + 15) {
      cars[i].y = -15 - random(10, 40);
      cars[i].lane = random(0, 3);
    }

    // AABB Collision check
    int cx = LANE_CENTERS[cars[i].lane];
    int cy = (int)cars[i].y;

    if (abs((int)playerX - cx) < ((BIKE_W + CAR_W) / 2 - 1) &&
        abs(playerY - cy) < ((BIKE_H + CAR_H) / 2 - 2)) {
      gameOver = true;
    }
  }
}

void drawScene() {
  display.clearDisplay();

  // 1. Draw Road Boundaries
  display.drawLine(28, 0, 28, 63, SSD1306_WHITE);
  display.drawLine(29, 0, 29, 63, SSD1306_WHITE);
  display.drawLine(99, 0, 99, 63, SSD1306_WHITE);
  display.drawLine(100, 0, 100, 63, SSD1306_WHITE);

  // 2. Dashed Lane Dividers (16px cycle: 8px dash, 8px gap)
  for (int y = -16; y < SCREEN_HEIGHT; y += 16) {
    int curY = y + (int)roadScrollOffset;
    display.drawLine(53, curY, 53, curY + 7, SSD1306_WHITE);
    display.drawLine(75, curY, 75, curY + 7, SSD1306_WHITE);
  }

  // 3. Draw Traffic Cars
  for (int i = 0; i < MAX_CARS; i++) {
    if (cars[i].y > -15 && cars[i].y < SCREEN_HEIGHT) {
      int cx = LANE_CENTERS[cars[i].lane] - (CAR_W / 2);
      int cy = (int)cars[i].y;

      // Chassis
      display.drawRoundRect(cx, cy, CAR_W, CAR_H, 2, SSD1306_WHITE);
      // Windshield
      display.drawFastHLine(cx + 2, cy + 3, CAR_W - 4, SSD1306_WHITE);
      // Taillights
      display.drawPixel(cx + 1, cy + CAR_H - 1, SSD1306_WHITE);
      display.drawPixel(cx + CAR_W - 2, cy + CAR_H - 1, SSD1306_WHITE);
    }
  }

  // 4. Draw Motorcycle (Detailed 1-bit sprite)
  int bx = (int)playerX - (BIKE_W / 2);
  int by = playerY - (BIKE_H / 2);

  // Front wheel & Rear wheel
  display.drawFastVLine(bx + 3, by, 3, SSD1306_WHITE);
  display.drawFastVLine(bx + 3, by + 8, 3, SSD1306_WHITE);
  // Handlebars
  display.drawFastHLine(bx, by + 3, 7, SSD1306_WHITE);
  // Chassis & Rider Torso
  display.fillRect(bx + 2, by + 4, 3, 4, SSD1306_WHITE);
  // Helmet
  display.drawPixel(bx + 3, by + 4, SSD1306_BLACK);

  // 5. Left HUD (Score & Speed)
  display.setTextSize(1);
  display.setCursor(0, 4);
  display.print(F("SCR"));
  display.setCursor(0, 14);
  display.print(score / 10);

  display.setCursor(0, 34);
  display.print(F("SPD"));
  display.setCursor(0, 44);
  display.print((int)currentSpeed);

  // 6. Right HUD (Boost Bar & Multiplier)
  display.setCursor(104, 4);
  display.print(F("BST"));
  display.drawRect(104, 14, 20, 5, SSD1306_WHITE);
  display.fillRect(106, 16, 14, 2, SSD1306_WHITE);

  display.setCursor(106, 34);
  display.print(F("x5"));

  // 7. Game Over Screen
  if (gameOver) {
    display.fillRect(16, 18, 96, 28, SSD1306_BLACK);
    display.drawRect(16, 18, 96, 28, SSD1306_WHITE);
    display.setCursor(24, 23);
    display.print(F("CRASHED!"));
    display.setCursor(22, 34);
    display.print(F("TAP TO RETRY"));
  }

  display.display();
}

void loop() {
  server.handleClient();
  updatePhysics();
  drawScene();
}
