#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Direct GPIO mapping for Seeed Studio XIAO ESP32-C6
// XIAO C6 Pin Map:
//   D6  = GPIO16 (UART TX)
//   D7  = GPIO17 (UART RX) -> OLED SDA (default)
//   D8  = GPIO19 (SPI SCK) -> OLED SCL (default)
//   D9  = GPIO20 (SPI MISO) -> Piezo/Sound
#define SOUND_PIN   20  // GPIO20 (D9 on XIAO C6) -> Transistor Base driving piezo
#define LED_PIN     15  // GPIO15                 -> On-board LED
#define OLED_PIN_D7 17  // GPIO17 (D7 on XIAO C6)
#define OLED_PIN_D8 19  // GPIO19 (D8 on XIAO C6)

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1

#define IS_SINGLE_NETWORK true
#define SINGLE_NETWORK_SSID String("mynet")
#define IS_PRINT_NETWORKS false

// --- Fixed Acoustic Properties ---
const int BEEP_TONE_FREQ        = 2700;   // Resonant frequency for piezo click (Hz)
const int BEEP_DURATION_MS      = 12;     // Duration of a single click

// --- Sensitivity Tuning ---
const float MAX_DELAY_IDLE_MS   = 2000.0f; // Background radiation (slow ticking)
const float MIN_DELAY_ACTIVE_MS = 5.0f;    // Full meltdown chatter rate

// --- Global State ---
volatile float currentMaxDelay = MAX_DELAY_IDLE_MS;
volatile float currentDelay    = MAX_DELAY_IDLE_MS;
volatile bool isScanning       = false;

unsigned long scanCount = 0;
int lastRSSI = -100;
unsigned long lastBeep = 0;

int activeSDA = OLED_PIN_D7;
int activeSCL = OLED_PIN_D8;
uint8_t activeOledAddr = 0x3C;
bool oledFound = false;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool checkI2CAddress(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

void initOLED() {
    // 1. Try candidate 1: D7 = SDA, D8 = SCL
    Wire.begin(OLED_PIN_D7, OLED_PIN_D8, 400000);
    delay(20);
    if (checkI2CAddress(0x3C)) {
        activeSDA = OLED_PIN_D7;
        activeSCL = OLED_PIN_D8;
        activeOledAddr = 0x3C;
        oledFound = true;
    } else if (checkI2CAddress(0x3D)) {
        activeSDA = OLED_PIN_D7;
        activeSCL = OLED_PIN_D8;
        activeOledAddr = 0x3D;
        oledFound = true;
    } else {
        // 2. Try candidate 2: D8 = SDA, D7 = SCL (swapped wiring)
        Wire.end();
        Wire.begin(OLED_PIN_D8, OLED_PIN_D7, 400000);
        delay(20);
        if (checkI2CAddress(0x3C)) {
            activeSDA = OLED_PIN_D8;
            activeSCL = OLED_PIN_D7;
            activeOledAddr = 0x3C;
            oledFound = true;
        } else if (checkI2CAddress(0x3D)) {
            activeSDA = OLED_PIN_D8;
            activeSCL = OLED_PIN_D7;
            activeOledAddr = 0x3D;
            oledFound = true;
        }
    }

    if (oledFound) {
        Serial.printf("[OLED] Detected SSD1306 at 0x%02X (SDA=GPIO%d, SCL=GPIO%d)\n",
                      activeOledAddr, activeSDA, activeSCL);
        if (!display.begin(SSD1306_SWITCHCAPVCC, activeOledAddr, false, false)) {
            Serial.println("[OLED] SSD1306 display.begin() failed!");
            oledFound = false;
        }
    } else {
        Serial.println("[OLED] Warning: No OLED detected at 0x3C/0x3D on D7/D8. Defaulting to D7=SDA, D8=SCL.");
        Wire.end();
        Wire.begin(OLED_PIN_D7, OLED_PIN_D8, 400000);
        display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);
    }
}

void showSplashScreen() {
    if (!oledFound) return;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(18, 2);
    display.print(F("RAD-RADAR C6"));

    display.drawFastHLine(0, 12, 128, SSD1306_WHITE);

    display.setCursor(4, 16);
    if (oledFound) {
        display.printf("OLED:0x%02X D%d/D%d", activeOledAddr,
                       (activeSDA == OLED_PIN_D7 ? 7 : 8),
                       (activeSCL == OLED_PIN_D8 ? 8 : 7));
    } else {
        display.print(F("OLED: Not Found"));
    }

    display.setCursor(4, 24);
    display.print(F("Starting scan..."));
    display.display();
}

#define GRAPH_CAPACITY 80
int rssiHistory[GRAPH_CAPACITY];
int historyHead = 0;
int historyCount = 0;

void addHistory(int val) {
    rssiHistory[historyHead] = val;
    historyHead = (historyHead + 1) % GRAPH_CAPACITY;
    if (historyCount < GRAPH_CAPACITY) {
        historyCount++;
    }
}

int getHistory(int indexFromOldest) {
    int start = (historyCount < GRAPH_CAPACITY) ? 0 : historyHead;
    int idx = (start + indexFromOldest) % GRAPH_CAPACITY;
    return rssiHistory[idx];
}

void updateDisplay() {
    if (!oledFound) return;

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // 1. Left side: Extra-large numeric readout
    if (lastRSSI <= -100) {
        display.setTextSize(4);
        display.setCursor(0, 2);
        display.print(F("--"));
    } else {
        int val = abs(lastRSSI);
        String s = String(val);
        if (s.length() <= 2) {
            display.setTextSize(4);
            display.setCursor(0, 2);
            display.print(s);
        } else {
            display.setTextSize(3);
            display.setCursor(0, 5);
            display.print(s);
        }
    }

    // 2. Right side: Borderless edge-to-edge Historical Chart (x=48 to 127, y=0 to 31)
    if (historyCount > 0) {
        int prevX = -1;
        int prevY = -1;

        for (int i = 0; i < historyCount; i++) {
            int val = getHistory(i);
            int constrained = constrain(val, -95, -30);
            int y = map(constrained, -95, -30, 31, 0);
            int x = 127 - (historyCount - 1 - i);

            display.drawPixel(x, y, SSD1306_WHITE);
            if (prevX != -1) {
                display.drawLine(prevX, prevY, x, y, SSD1306_WHITE);
            }
            prevX = x;
            prevY = y;
        }
    }

    display.display();
}

void generateCurrentDelay() {
    long upperBound = max(7L, (long)currentMaxDelay);
    currentDelay = random(3, upperBound);
}

void processScanResults(int networksFound) {
    int strongestRSSI = -100; // Weak signal baseline

    for (int i = 0; i < networksFound; i++) {
        if (IS_SINGLE_NETWORK && !(SINGLE_NETWORK_SSID.equals(WiFi.SSID(i)))) {
            continue;
        }

        int rssi = WiFi.RSSI(i);
        if (rssi > strongestRSSI) {
            strongestRSSI = rssi; // Finds strongest AP
        }

        if (IS_PRINT_NETWORKS) {
            Serial.printf("SSID: %s | RSSI: %d dBm\n", WiFi.SSID(i).c_str(), rssi);
        }
    }

    lastRSSI = strongestRSSI;
    addHistory(strongestRSSI);
    scanCount++;

    // Clean up scanner memory
    WiFi.scanDelete();

    // Map RSSI (-90 dBm weak to -30 dBm strong) to a 0.0 -> 1.0 scale
    int constrainedRSSI = constrain(strongestRSSI, -90, -30);
    float normalized = (float)(constrainedRSSI - (-90)) / (float)(-30 - (-90));

    // Logarithmic decay curve: creates steep response spikes near strong signals
    float logFactor = log10(1.0f + 9.0f * normalized); // 0.0 -> 1.0 curve

    // Map max delay using the logarithmic curve
    currentMaxDelay = MAX_DELAY_IDLE_MS - (logFactor * (MAX_DELAY_IDLE_MS - MIN_DELAY_ACTIVE_MS));

    Serial.printf("Strongest RSSI: %d dBm | MaxDelay: %.0f ms\n", strongestRSSI, currentMaxDelay);

    updateDisplay();

    WiFi.scanNetworks(true);
    isScanning = true;
}

void click(int durationMs) {
    // Single fixed-frequency acoustic click
    // tone(SOUND_PIN, BEEP_TONE_FREQ, durationMs);

    // Active LOW LED flash
    digitalWrite(SOUND_PIN, HIGH);  
    digitalWrite(LED_PIN, LOW);   
    delay(durationMs);             
    digitalWrite(SOUND_PIN, LOW);          
    digitalWrite(LED_PIN, HIGH);  
}

void setup() {
    Serial.begin(115200);

    pinMode(SOUND_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);

    digitalWrite(SOUND_PIN, LOW);
    digitalWrite(LED_PIN, HIGH); // LED OFF

    // Initialize OLED with pin auto-detection on D7/D8
    initOLED();
    showSplashScreen();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    delay(1000); // Allow splash screen to be visible briefly

    // Start background scan
    WiFi.scanNetworks(true);
    isScanning = true;
    updateDisplay();

    generateCurrentDelay();
}

void loop() {
    unsigned long now = millis();
    long elapsed = now - lastBeep;

    // 1. Audio Click Engine
    if (elapsed > currentDelay) {
        lastBeep = now;

        // Near strong APs (< 30ms max delay), simulate chaotic multi-particle bursts
        if (currentMaxDelay < 30.0f && random(0, 100) > 40) {
            click(8);
            delay(3);
            click(6);
        } else {
            click(BEEP_DURATION_MS);
        }

        generateCurrentDelay();
    }

    // 2. Non-Blocking Async WiFi Scanner Check
    if (isScanning) {
        int16_t scanStatus = WiFi.scanComplete();
        if (scanStatus >= 0) {
            isScanning = false;
            processScanResults(scanStatus);
        } else if (scanStatus == WIFI_SCAN_FAILED) {
            WiFi.scanDelete();
            WiFi.scanNetworks(true);
        }
    }
}