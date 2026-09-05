#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences prefs;

// --- Wi-Fi SoftAP Configuration ---
const char* apSSID = "OverTake";
const char* apPass = "12345678";
WebServer server(80);

// --- Vertical Geometry (Rotated 64 x 128) ---
const int V_WIDTH  = 64;
const int V_HEIGHT = 128;

// Road boundaries (X: 11 to 52)
const int ROAD_LEFT_BOUND  = 16;
const int ROAD_RIGHT_BOUND = 47;
const int LANE_CENTERS[3]  = {19, 32, 45};

// Bigger Motorcycle Dimensions
const int BIKE_W = 11;
const int BIKE_H = 18;

// --- Speed Wind Streaks & Skid Marks ---
struct WindLine {
  float x;
  float y;
  float len;
  float speed;
};
const int NUM_WIND = 6;
WindLine windLines[NUM_WIND];

struct SkidMark {
  float x;
  float y;
  int len;
};
const int NUM_SKIDS = 3;
SkidMark skids[NUM_SKIDS];

// --- Terrain / Biome Definitions (20 Stages for 2 KM) ---
enum TerrainType {
  TERRAIN_CITY = 0,
  TERRAIN_BRIDGE = 1,
  TERRAIN_DESERT = 2,
  TERRAIN_HIGHWAY = 3,
  TERRAIN_FOREST = 4,
  TERRAIN_TUNNEL = 5,
  TERRAIN_SPEEDWAY = 6
};

const char* TERRAIN_NAMES[20] = {
  "CITY", "BRIDGE", "DESERT", "HIGHWAY", "FOREST",
  "TUNNEL", "METRO", "COAST", "CANYON", "SPEEDWAY",
  "NEON", "SNOW", "OASIS", "SKYWAY", "VALLEY",
  "SUBWAY", "DOCKS", "FACTORY", "RIDGE", "FINALE"
};

int currentTerrain = 0;
int lastTerrain = -1;
unsigned long zoneBannerUntil = 0;

// --- Traffic Vehicles ---
enum CarType {
  CAR_SEDAN = 0,
  CAR_TRUCK = 1,
  CAR_SUV   = 2
};

struct Car {
  float x;
  float y;
  int currentLane;
  int targetLane;
  CarType type;
  bool active;
  bool changingLane;
  unsigned long signalStartTime;
  float triggerY;
};

const int MAX_CARS = 2;
Car cars[MAX_CARS];

// --- Player State & Leaning Physics ---
float playerX = 32.0;
float targetX = 32.0;
float currentLean = 0.0; // Dynamic tilt offset: -3.0 (left) to +3.0 (right)
const int playerY = 94;

int lives = 3;
unsigned long invulnerableUntil = 0;

float baseSpeed = 70.0;
float currentSpeed = 70.0;
bool isBoosting = false;
unsigned long score = 0;
unsigned long highScore = 0;

float distanceMeters = 0.0;
int nextMilestone = 100;
const float FINISH_DISTANCE = 2000.0;
float finishLineY = -40.0;
bool finishLineActive = false;

bool gameOver = false;
bool gameWon = false;
float roadScrollOffset = 0.0;
float sideScrollOffset = 0.0;
bool wasConnected = false;

// --- Web Controller Interface ---
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>OverTake Controller</title>
  <style>
    * { box-sizing: border-box; touch-action: none; -webkit-touch-callout: none; }
    body {
      margin: 0; background: #08090d; color: #fff;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      height: 100vh; overflow: hidden; display: flex; flex-direction: column;
      user-select: none; -webkit-user-select: none;
    }
    #steer-zone {
      flex: 3; margin: 10px; border-radius: 20px;
      background: #121620; border: 2px solid #00e5ff;
      display: flex; flex-direction: column; align-items: center; justify-content: center;
      position: relative;
    }
    #handle {
      width: 58px; height: 58px; border-radius: 50%;
      background: #00e5ff; box-shadow: 0 0 20px #00e5ff;
      position: absolute; pointer-events: none;
      transform: translate(-50%, -50%);
      left: 50%; top: 50%;
    }
    #boost-btn {
      flex: 1.2; margin: 0 10px 10px 10px; border-radius: 18px;
      background: #d50000; color: white; font-size: 1.9rem; font-weight: 900;
      display: flex; align-items: center; justify-content: center;
      border: 2px solid #ff1744; letter-spacing: 2px;
    }
    #boost-btn:active, #boost-btn.active {
      background: #ff1744; box-shadow: 0 0 30px #ff1744;
    }
    .label { color: #8090a6; font-size: 0.85rem; pointer-events: none; }
    #status { font-size: 0.8rem; color: #00e5ff; position: absolute; bottom: 8px; }
  </style>
</head>
<body>
  <div id="steer-zone">
    <div id="handle"></div>
    <div class="label">DRAG THUMB TO STEER & LEAN</div>
    <div id="status">STEER: 0.00</div>
  </div>
  <div id="boost-btn">HOLD TO BOOST</div>

  <script>
    const steerZone = document.getElementById('steer-zone');
    const handle = document.getElementById('handle');
    const boostBtn = document.getElementById('boost-btn');
    const status = document.getElementById('status');

    let steerPos = 0;
    let boostActive = 0;
    let sending = false;

    function updateSteer(clientX) {
      const rect = steerZone.getBoundingClientRect();
      let x = clientX - rect.left;
      x = Math.max(28, Math.min(rect.width - 28, x));
      handle.style.left = x + 'px';
      steerPos = ((x - 28) / (rect.width - 56)) * 2 - 1;
      status.innerText = `STEER: ${steerPos.toFixed(2)}`;
    }

    steerZone.addEventListener('pointerdown', (e) => updateSteer(e.clientX));
    steerZone.addEventListener('pointermove', (e) => { if (e.buttons > 0) updateSteer(e.clientX); });
    steerZone.addEventListener('pointerup', () => {
      handle.style.left = '50%';
      steerPos = 0;
      status.innerText = 'STEER: 0.00';
    });

    boostBtn.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      boostActive = 1;
      boostBtn.classList.add('active');
      if (navigator.vibrate) navigator.vibrate(35);
    });

    boostBtn.addEventListener('pointerup', (e) => {
      e.preventDefault();
      boostActive = 0;
      boostBtn.classList.remove('active');
    });

    boostBtn.addEventListener('pointercancel', () => {
      boostActive = 0;
      boostBtn.classList.remove('active');
    });

    setInterval(() => {
      if (sending) return;
      sending = true;
      fetch(`/cmd?s=${steerPos.toFixed(2)}&b=${boostActive}`, { method: 'POST' })
        .catch(() => {})
        .finally(() => { sending = false; });
    }, 60);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleCommand() {
  if (gameOver || gameWon) {
    if (server.hasArg("b") && server.arg("b").toInt() == 1) {
      resetGame();
    }
  } else {
    if (server.hasArg("s")) {
      float steer = server.arg("s").toFloat();
      targetX = map(steer * 100, -100, 100, ROAD_LEFT_BOUND, ROAD_RIGHT_BOUND);
    }
    if (server.hasArg("b")) {
      isBoosting = (server.arg("b").toInt() == 1);
    }
  }
  server.send(200, "text/plain", "OK");
}

int getCarWidth(CarType t) {
  switch (t) {
    case CAR_TRUCK: return 8;
    case CAR_SUV:   return 9;
    case CAR_SEDAN: default: return 8;
  }
}

int getCarHeight(CarType t) {
  switch (t) {
    case CAR_TRUCK: return 28;
    case CAR_SUV:   return 14;
    case CAR_SEDAN: default: return 16;
  }
}

void spawnCar(int i, float startY) {
  cars[i].currentLane = random(0, 3);
  cars[i].x = LANE_CENTERS[cars[i].currentLane];
  cars[i].y = startY;
  cars[i].type = (CarType)random(0, 3);
  cars[i].active = true;
  cars[i].changingLane = false;
  cars[i].signalStartTime = 0;

  if (random(0, 100) < 80) {
    int curL = cars[i].currentLane;
    if (curL == 0)      cars[i].targetLane = 1;
    else if (curL == 2) cars[i].targetLane = 1;
    else                cars[i].targetLane = (random(0, 2) == 0) ? 0 : 2;

    cars[i].triggerY = (float)random(20, 45);
  } else {
    cars[i].targetLane = cars[i].currentLane;
    cars[i].triggerY = 999.0;
  }
}

void resetGame() {
  playerX = 32.0;
  targetX = 32.0;
  currentLean = 0.0;
  baseSpeed = 70.0;
  currentSpeed = 70.0;
  isBoosting = false;
  score = 0;
  lives = 3;
  invulnerableUntil = 0;

  distanceMeters = 0.0;
  nextMilestone = 100;
  finishLineY = -40.0;
  finishLineActive = false;

  currentTerrain = 0;
  lastTerrain = -1;
  zoneBannerUntil = millis() + 1800;

  gameOver = false;
  gameWon = false;

  // Initialize wind lines & skid marks
  for (int i = 0; i < NUM_WIND; i++) {
    windLines[i].x = random(ROAD_LEFT_BOUND - 2, ROAD_RIGHT_BOUND + 2);
    windLines[i].y = random(0, V_HEIGHT);
    windLines[i].len = random(8, 22);
    windLines[i].speed = random(6, 12);
  }

  for (int i = 0; i < NUM_SKIDS; i++) {
    skids[i].x = LANE_CENTERS[random(0, 3)] + random(-3, 3);
    skids[i].y = random(0, V_HEIGHT);
    skids[i].len = random(10, 20);
  }

  spawnCar(0, -20);
  spawnCar(1, -75);
}

void checkAndSaveHighScore() {
  if (score > highScore) {
    highScore = score;
    prefs.putULong("high", highScore);
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    for (;;);
  }

  display.setRotation(1); // Portrait (64x128)

  prefs.begin("overtake", false);
  highScore = prefs.getULong("high", 0);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  WiFi.softAP(apSSID, apPass);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/cmd", HTTP_POST, handleCommand);
  server.begin();

  resetGame();
}

void updatePhysics() {
  if (gameOver || gameWon) return;

  // Lateral movement calculation
  float prevX = playerX;
  playerX += (targetX - playerX) * 0.70;

  // Calculate dynamic leaning angle based on steering velocity
  float steerVelocity = (playerX - prevX);
  float targetLean = constrain(steerVelocity * 1.8, -3.0, 3.0);
  currentLean += (targetLean - currentLean) * 0.40; // Smooth banking weight transfer

  currentSpeed = isBoosting ? (baseSpeed + 65.0) : baseSpeed;

  float dy = currentSpeed / 20.0;
  float meterDelta = dy * 0.18;
  distanceMeters += meterDelta;
  score += (unsigned long)(dy * (isBoosting ? 10 : 2));

  // Update skid marks scrolling down the asphalt
  for (int i = 0; i < NUM_SKIDS; i++) {
    skids[i].y += dy;
    if (skids[i].y > V_HEIGHT + 25) {
      skids[i].y = -random(20, 50);
      skids[i].x = LANE_CENTERS[random(0, 3)] + random(-3, 3);
      skids[i].len = random(10, 20);
    }
  }

  // Update speed wind lines during boost
  if (isBoosting) {
    for (int i = 0; i < NUM_WIND; i++) {
      windLines[i].y += dy * 2.2 + windLines[i].speed;
      if (windLines[i].y > V_HEIGHT + 20) {
        windLines[i].y = -random(10, 40);
        windLines[i].x = random(ROAD_LEFT_BOUND - 3, ROAD_RIGHT_BOUND + 3);
        windLines[i].len = random(12, 28);
      }
    }
  }

  // Determine stage & milestone speed ramp
  currentTerrain = (int)(distanceMeters / 100.0);
  if (currentTerrain > 19) currentTerrain = 19;

  if (currentTerrain != lastTerrain) {
    lastTerrain = currentTerrain;
    zoneBannerUntil = millis() + 1600;
  }

  if (distanceMeters >= nextMilestone && nextMilestone < FINISH_DISTANCE) {
    if (baseSpeed < 220.0) baseSpeed *= 1.10;
    nextMilestone += 100;
  }

  if (distanceMeters >= FINISH_DISTANCE) {
    finishLineActive = true;
  }

  roadScrollOffset += dy;
  if (roadScrollOffset >= 16.0) roadScrollOffset = 0.0;

  sideScrollOffset += dy;
  if (sideScrollOffset >= 32.0) sideScrollOffset -= 32.0;

  if (finishLineActive) {
    finishLineY += dy;
    if (finishLineY >= playerY - 4) {
      gameWon = true;
      checkAndSaveHighScore();
      return;
    }
  }

  // Update Traffic
  for (int i = 0; i < MAX_CARS; i++) {
    int carH = getCarHeight(cars[i].type);
    int carW = getCarWidth(cars[i].type);

    if (finishLineActive && cars[i].y > V_HEIGHT + 10) {
      cars[i].active = false;
      continue;
    }

    cars[i].y += dy * 0.48;

    if (cars[i].y > V_HEIGHT + carH + 10) {
      spawnCar(i, -carH - random(20, 60));
    }

    if (!cars[i].changingLane && cars[i].targetLane != cars[i].currentLane && cars[i].y >= cars[i].triggerY) {
      cars[i].changingLane = true;
      cars[i].signalStartTime = millis();
    }

    if (cars[i].changingLane) {
      if (millis() - cars[i].signalStartTime > 260) {
        float targetCenterX = LANE_CENTERS[cars[i].targetLane];
        float shiftSpeed = 2.0;

        if (cars[i].x < targetCenterX) {
          cars[i].x += shiftSpeed;
          if (cars[i].x >= targetCenterX) {
            cars[i].x = targetCenterX;
            cars[i].currentLane = cars[i].targetLane;
            cars[i].changingLane = false;
          }
        } else if (cars[i].x > targetCenterX) {
          cars[i].x -= shiftSpeed;
          if (cars[i].x <= targetCenterX) {
            cars[i].x = targetCenterX;
            cars[i].currentLane = cars[i].targetLane;
            cars[i].changingLane = false;
          }
        }
      }
    }

    // AABB Collision Check
    if (millis() > invulnerableUntil) {
      if (abs((int)playerX - (int)cars[i].x) < ((BIKE_W + carW) / 2 - 2) &&
          abs(playerY - (int)cars[i].y) < ((BIKE_H + carH) / 2 - 3)) {
        
        lives--;
        invulnerableUntil = millis() + 1800;
        cars[i].y += 35;

        if (lives <= 0) {
          gameOver = true;
          isBoosting = false;
          checkAndSaveHighScore();
        }
      }
    }
  }
}

// 5x4 Heart icon for lives
void drawHeart(int x, int y) {
  display.drawPixel(x + 1, y, SSD1306_WHITE);
  display.drawPixel(x + 3, y, SSD1306_WHITE);
  display.drawLine(x, y + 1, x + 4, y + 1, SSD1306_WHITE);
  display.drawLine(x, y + 2, x + 4, y + 2, SSD1306_WHITE);
  display.drawPixel(x + 2, y + 3, SSD1306_WHITE);
}

// Draw rich, detailed motorcycle with live tilt banking
void drawMotorcycle(int cx, int cy, float lean, bool boost) {
  int l = (int)round(lean); // -3 (full left lean) to +3 (full right lean)

  // 1. Fat Rear Wheel (centered on ground contact patch)
  display.fillRoundRect(cx - 2, cy + 3, 5, 7, 1, SSD1306_WHITE);
  display.drawFastVLine(cx, cy + 4, 5, SSD1306_BLACK); // Wheel tread line

  // 2. Dual Rear Exhaust Pipes & Nitro Thrusters
  display.drawFastVLine(cx - 3, cy + 5, 4, SSD1306_WHITE);
  display.drawFastVLine(cx + 3, cy + 5, 4, SSD1306_WHITE);

  // Blazing dual nitro flames when boosting
  if (boost && ((millis() / 35) % 2 == 0)) {
    display.drawLine(cx - 3, cy + 10, cx - 3 + (l / 2), cy + 16, SSD1306_WHITE);
    display.drawLine(cx + 3, cy + 10, cx + 3 + (l / 2), cy + 16, SSD1306_WHITE);
    display.drawPixel(cx - 2, cy + 12, SSD1306_WHITE);
    display.drawPixel(cx + 2, cy + 12, SSD1306_WHITE);
  }

  // 3. Main Chassis & Fuel Tank (banks with lean)
  display.fillRect(cx - 2 + l, cy - 2, 5, 6, SSD1306_WHITE);

  // 4. Handlebars & Mirrors
  display.drawFastHLine(cx - 5 + l, cy - 4, 11, SSD1306_WHITE);
  display.drawPixel(cx - 5 + l, cy - 5, SSD1306_WHITE); // Left mirror
  display.drawPixel(cx + 5 + l, cy - 5, SSD1306_WHITE); // Right mirror

  // 5. Front Wheel (angled)
  display.drawFastVLine(cx + l, cy - 8, 4, SSD1306_WHITE);

  // 6. Rider Torso & Racing Leathers (shoulders lean dynamically)
  display.fillRoundRect(cx - 4 + l, cy - 3, 9, 6, 2, SSD1306_WHITE);
  display.drawFastHLine(cx - 2 + l, cy + 1, 5, SSD1306_BLACK); // Racing belt cut

  // 7. Rider Helmet & Visor Highlight
  display.fillCircle(cx + l, cy - 5, 3, SSD1306_WHITE);
  display.drawPixel(cx + l + (l > 0 ? 1 : (l < 0 ? -1 : 0)), cy - 5, SSD1306_BLACK); // Visor line

  // 8. Aerodynamic Wind Wake Cone when Boosting (matches reference photo)
  if (boost) {
    // Air cutting streamline cone around the bike
    display.drawLine(cx + l, cy - 14, cx - 7 + l, cy + 10, SSD1306_WHITE);
    display.drawLine(cx + l, cy - 14, cx + 7 + l, cy + 10, SSD1306_WHITE);
    if ((millis() / 40) % 2 == 0) {
      display.drawLine(cx - 8 + l, cy + 2, cx - 10 + l, cy + 14, SSD1306_WHITE);
      display.drawLine(cx + 8 + l, cy + 2, cx + 10 + l, cy + 14, SSD1306_WHITE);
    }
  }
}

// Side Scenery
void drawSideTerrain(int terrainType, float offset) {
  int t = terrainType % 7;

  for (int y = -32; y < V_HEIGHT; y += 32) {
    int curY = y + (int)offset;

    if (t == TERRAIN_CITY) {
      display.drawRect(1, curY, 8, 28, SSD1306_WHITE);
      display.drawRect(55, curY, 8, 28, SSD1306_WHITE);
      display.drawPixel(3, curY + 5, SSD1306_WHITE);
      display.drawPixel(6, curY + 5, SSD1306_WHITE);
      display.drawPixel(3, curY + 12, SSD1306_WHITE);
      display.drawPixel(6, curY + 12, SSD1306_WHITE);
      display.drawPixel(57, curY + 5, SSD1306_WHITE);
      display.drawPixel(60, curY + 5, SSD1306_WHITE);
    }
    else if (t == TERRAIN_BRIDGE) {
      display.drawFastHLine(1, curY + 4, 5, SSD1306_WHITE);
      display.drawFastHLine(3, curY + 18, 5, SSD1306_WHITE);
      display.drawFastHLine(56, curY + 8, 5, SSD1306_WHITE);
      display.drawFastVLine(9, curY, 32, SSD1306_WHITE);
      display.drawFastVLine(54, curY, 32, SSD1306_WHITE);
      display.drawLine(2, curY, 9, curY + 16, SSD1306_WHITE);
      display.drawLine(61, curY, 54, curY + 16, SSD1306_WHITE);
    }
    else if (t == TERRAIN_DESERT) {
      display.drawFastVLine(5, curY + 6, 14, SSD1306_WHITE);
      display.drawLine(2, curY + 10, 4, curY + 10, SSD1306_WHITE);
      display.drawFastVLine(2, curY + 7, 3, SSD1306_WHITE);
      display.drawRoundRect(55, curY + 16, 7, 5, 2, SSD1306_WHITE);
    }
    else if (t == TERRAIN_HIGHWAY) {
      display.drawRect(1, curY + 4, 8, 16, SSD1306_WHITE);
      display.drawFastHLine(3, curY + 10, 4, SSD1306_WHITE);
      display.drawRect(55, curY + 4, 8, 16, SSD1306_WHITE);
    }
    else if (t == TERRAIN_FOREST) {
      display.drawLine(5, curY + 2, 2, curY + 12, SSD1306_WHITE);
      display.drawLine(5, curY + 2, 8, curY + 12, SSD1306_WHITE);
      display.drawFastHLine(2, curY + 12, 7, SSD1306_WHITE);
      display.drawFastVLine(5, curY + 13, 4, SSD1306_WHITE);
      display.drawLine(59, curY + 10, 56, curY + 20, SSD1306_WHITE);
      display.drawLine(59, curY + 10, 62, curY + 20, SSD1306_WHITE);
      display.drawFastHLine(56, curY + 20, 7, SSD1306_WHITE);
    }
    else if (t == TERRAIN_TUNNEL) {
      display.fillRect(0, curY, 10, 4, SSD1306_WHITE);
      display.fillRect(54, curY, 10, 4, SSD1306_WHITE);
      display.drawPixel(9, curY + 14, SSD1306_WHITE);
      display.drawPixel(54, curY + 14, SSD1306_WHITE);
    }
    else {
      display.drawRect(1, curY + 2, 7, 10, SSD1306_WHITE);
      display.fillRect(56, curY + 4, 6, 6, SSD1306_WHITE);
      display.drawFastVLine(59, curY + 10, 12, SSD1306_WHITE);
    }
  }
}

// Detailed Traffic Vehicles
void drawCar(const Car& c) {
  int cw = getCarWidth(c.type);
  int ch = getCarHeight(c.type);
  int cx = (int)c.x - (cw / 2);
  int cy = (int)c.y - (ch / 2);

  if (cy + ch < 0 || cy > V_HEIGHT) return;

  if (c.type == CAR_SEDAN) {
    display.drawRoundRect(cx, cy, cw, ch, 2, SSD1306_WHITE);
    // Front & rear windshields
    display.drawFastHLine(cx + 1, cy + 3, cw - 2, SSD1306_WHITE);
    display.drawFastHLine(cx + 1, cy + ch - 4, cw - 2, SSD1306_WHITE);
    // Rear spoiler & side mirrors
    display.drawFastHLine(cx, cy + ch - 1, cw, SSD1306_WHITE);
    display.drawPixel(cx - 1, cy + 4, SSD1306_WHITE);
    display.drawPixel(cx + cw, cy + 4, SSD1306_WHITE);
  } 
  else if (c.type == CAR_TRUCK) {
    display.drawRect(cx, cy, cw, ch, SSD1306_WHITE);
    display.drawFastHLine(cx + 1, cy + 5, cw - 2, SSD1306_WHITE);
    display.fillRect(cx + 1, cy + 1, cw - 2, 3, SSD1306_WHITE); // Cab roof
    // Cargo ribbing lines
    for (int ly = cy + 9; ly < cy + ch - 2; ly += 5) {
      display.drawFastHLine(cx + 1, ly, cw - 2, SSD1306_WHITE);
    }
  } 
  else if (c.type == CAR_SUV) {
    display.drawRoundRect(cx, cy, cw, ch, 2, SSD1306_WHITE);
    display.drawRect(cx + 1, cy + 3, cw - 2, ch - 6, SSD1306_WHITE);
    // Roof luggage rack cross bars
    display.drawFastHLine(cx + 2, cy + 5, cw - 4, SSD1306_WHITE);
    display.drawFastHLine(cx + 2, cy + 9, cw - 4, SSD1306_WHITE);
  }

  // Turn signal flashers
  if (c.changingLane && ((millis() / 60) % 2 == 0)) {
    if (c.targetLane < c.currentLane) {
      display.fillRect(cx - 2, cy + 1, 2, 3, SSD1306_WHITE);
      display.fillRect(cx - 2, cy + ch - 4, 2, 3, SSD1306_WHITE);
    } else if (c.targetLane > c.currentLane) {
      display.fillRect(cx + cw, cy + 1, 2, 3, SSD1306_WHITE);
      display.fillRect(cx + cw, cy + ch - 4, 2, 3, SSD1306_WHITE);
    }
  }
}

void drawWaitingScreen() {
  display.clearDisplay();
  display.drawRect(0, 0, V_WIDTH, V_HEIGHT, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(5, 8);
  display.print(F("OVERTAKE"));
  display.drawFastHLine(4, 18, 56, SSD1306_WHITE);

  display.setCursor(6, 26);
  display.print(F("CONNECT"));
  display.setCursor(6, 36);
  display.print(F("WI-FI:"));
  display.setCursor(6, 48);
  display.print(apSSID);

  display.setCursor(6, 64);
  display.print(F("PASS:"));
  display.setCursor(6, 74);
  display.print(apPass);

  display.setCursor(6, 92);
  display.print(F("OPEN:"));
  display.setCursor(2, 102);
  display.print(F("192.168.4.1"));

  if ((millis() / 350) % 2 == 0) {
    display.fillRect(28, 116, 8, 4, SSD1306_WHITE);
  }

  display.display();
}

void drawScene() {
  display.clearDisplay();

  // 1. Side Scenery
  drawSideTerrain(currentTerrain, sideScrollOffset);

  // 2. Beveled Highway Curbs & Rumble Strips (X: 10 & 53)
  display.drawFastVLine(10, 0, V_HEIGHT, SSD1306_WHITE);
  display.drawFastVLine(53, 0, V_HEIGHT, SSD1306_WHITE);
  for (int y = -8; y < V_HEIGHT; y += 8) {
    int curY = y + (int)roadScrollOffset;
    display.drawPixel(9, curY, SSD1306_WHITE);
    display.drawPixel(54, curY, SSD1306_WHITE);
  }

  // 3. Asphalt Skid Marks (Rubber streaks matching screenshot)
  for (int i = 0; i < NUM_SKIDS; i++) {
    if (skids[i].y > -10 && skids[i].y < V_HEIGHT) {
      display.drawLine((int)skids[i].x, (int)skids[i].y, (int)skids[i].x + 1, (int)skids[i].y + skids[i].len, SSD1306_WHITE);
    }
  }

  // 4. Dashed Center Lane Dividers (X=25 and X=39)
  for (int y = -16; y < V_HEIGHT; y += 16) {
    int curY = y + (int)roadScrollOffset;
    display.drawLine(25, curY, 25, curY + 6, SSD1306_WHITE);
    display.drawLine(39, curY, 39, curY + 6, SSD1306_WHITE);
  }

  // 5. Speed Wind Streaks (Anime / Boost effect)
  if (isBoosting) {
    for (int i = 0; i < NUM_WIND; i++) {
      if (windLines[i].y > -10 && windLines[i].y < V_HEIGHT) {
        display.drawFastVLine((int)windLines[i].x, (int)windLines[i].y, (int)windLines[i].len, SSD1306_WHITE);
      }
    }
  }

  // 6. Checkered 2 KM Finish Line
  if (finishLineActive && finishLineY > -10 && finishLineY < V_HEIGHT + 10) {
    int fy = (int)finishLineY;
    for (int x = 11; x < 53; x += 4) {
      display.fillRect(x, fy, 2, 2, SSD1306_WHITE);
      display.fillRect(x + 2, fy + 2, 2, 2, SSD1306_WHITE);
    }
  }

  // 7. Traffic Cars
  for (int i = 0; i < MAX_CARS; i++) {
    if (cars[i].active) {
      drawCar(cars[i]);
    }
  }

  // 8. Player Motorcycle (with dynamic leaning & boost flame)
  bool showBike = true;
  if (millis() < invulnerableUntil && ((millis() / 60) % 2 == 0)) {
    showBike = false;
  }

  if (showBike) {
    drawMotorcycle((int)playerX, playerY, currentLean, isBoosting);
  }

  // 9. Zone Milestone Notification
  if (millis() < zoneBannerUntil) {
    display.fillRect(6, 17, 52, 11, SSD1306_BLACK);
    display.drawRect(6, 17, 52, 11, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 19);
    display.print(TERRAIN_NAMES[currentTerrain]);
  }

  // 10. TOP HUD
  display.fillRect(0, 0, V_WIDTH, 14, SSD1306_BLACK);
  display.setTextSize(1);

  for (int i = 0; i < lives; i++) {
    drawHeart(1 + (i * 6), 1);
  }
  display.setCursor(1, 6);
  display.print(score / 10);

  // Boost Bar & Multiplier
  display.drawRect(36, 1, 27, 5, SSD1306_WHITE);
  if (isBoosting) {
    display.fillRect(37, 2, 25, 3, SSD1306_WHITE);
    display.setCursor(44, 7);
    display.print(F("x10"));
  } else {
    display.drawPixel(38, 2, SSD1306_WHITE);
    display.setCursor(48, 7);
    display.print(F("x5"));
  }
  display.drawFastHLine(0, 14, V_WIDTH, SSD1306_WHITE);

  // 11. BOTTOM HUD (Speedometer cluster matching screenshot)
  display.fillRect(0, 114, V_WIDTH, 14, SSD1306_BLACK);
  display.drawFastHLine(0, 113, V_WIDTH, SSD1306_WHITE);

  display.drawCircleHelper(32, 127, 20, 1 | 2, SSD1306_WHITE);
  display.setCursor(12, 118);
  display.print((int)currentSpeed);
  display.setCursor(34, 118);
  display.print(F("KM/H"));

  // 12. End Screens
  if (gameOver) {
    display.fillRect(4, 45, 56, 32, SSD1306_BLACK);
    display.drawRect(4, 45, 56, 32, SSD1306_WHITE);
    display.setCursor(7, 50);
    display.print(F("CRASHED"));
    display.setCursor(6, 64);
    display.print(F("TAP BOOST"));
  } else if (gameWon) {
    display.fillRect(4, 42, 56, 38, SSD1306_BLACK);
    display.drawRect(4, 42, 56, 38, SSD1306_WHITE);
    display.setCursor(7, 47);
    display.print(F("VICTORY!"));
    display.setCursor(11, 58);
    display.print(F("2KM WON"));
    display.setCursor(6, 70);
    display.print(F("TAP BOOST"));
  }

  display.display();
}

void loop() {
  server.handleClient();

  if (WiFi.softAPgetStationNum() == 0) {
    drawWaitingScreen();
    wasConnected = false;
    delay(20);
    return;
  }

  if (!wasConnected) {
    wasConnected = true;
    resetGame();
  }

  updatePhysics();
  drawScene();
}