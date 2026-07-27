#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

// Direct GPIO mapping for Seeed Studio XIAO ESP32-C6 / ESP32
#define SOUND_PIN 20  // GPIO20 (D9) -> Transistor Base driving piezo
#define LED_PIN   15  // GPIO15     -> On-board User LED

#define IS_SINGLE_NETWORK false
#define SINGLE_NETWORK_SSID String("myhomewifi")
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

unsigned long lastBeep = 0;

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

    // Restart non-blocking scan
    WiFi.scanNetworks(true);
    isScanning = true;
}

void click(int durationMs) {
    // Single fixed-frequency acoustic click
    tone(SOUND_PIN, BEEP_TONE_FREQ, durationMs);

    // Active LOW LED flash
    //digitalWrite(SOUND_PIN, HIGH);  
    digitalWrite(LED_PIN, LOW);   
    delay(durationMs);             
    //digitalWrite(SOUND_PIN, LOW);          
    digitalWrite(LED_PIN, HIGH);  
}

void setup() {
    Serial.begin(115200);

    pinMode(SOUND_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);

    digitalWrite(SOUND_PIN, LOW);
    digitalWrite(LED_PIN, HIGH); // LED OFF

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    delay(100);

    // Start background scan
    WiFi.scanNetworks(true);
    isScanning = true;

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