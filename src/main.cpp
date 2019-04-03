#include <Arduino.h>
#include "ESP8266WiFi.h"
#define SOUND_PIN 5 // this is the D1 pin on NodeMCU boards
#define LED_PIN 2   // embedded LED

#define IS_SINGLE_NETWORK false
#define SINGLE_NETWORK_SSID String("MyWifiNet")

#define IS_PRINT_NETWORKS false

volatile float maxDelay = 600000;
volatile float currentDelay = 600000;
volatile boolean isNeedScan = false;

unsigned long lastBeep = 0;

void generateCurrentDelay() {
    currentDelay = random(1, maxDelay);
}

float signalToMaxDelay(int signal) {
    return exp((signal - 15.) / 10.) * 5.;
}

void onCompleteScan(int networksFound) {
    float strongestSignal = 10000;

    for (int i =0; i < networksFound; i++) {
        if (IS_SINGLE_NETWORK && !(SINGLE_NETWORK_SSID.equals(WiFi.SSID(i)))) {
            continue;
        }

        int currentSignal = abs(WiFi.RSSI(i));
        if (currentSignal < strongestSignal) {
            strongestSignal = currentSignal;
        }
        
        if (IS_PRINT_NETWORKS) {
            Serial.print("Network name: ");
            Serial.println(WiFi.SSID(i));
            Serial.print("Signal strength: ");
            Serial.println(WiFi.RSSI(i));
        }
        
    }
    maxDelay = signalToMaxDelay(strongestSignal);
    generateCurrentDelay();
    isNeedScan = true;
}

void scanAsync() {
    isNeedScan = false;
    WiFi.scanNetworksAsync(onCompleteScan); 
}

void setup() {
    Serial.begin(9600);
    pinMode(SOUND_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(SOUND_PIN, LOW);
    digitalWrite(LED_PIN, HIGH);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    delay(1000);
 
    scanAsync(); 
}

void beep() {
    digitalWrite(SOUND_PIN, HIGH);
    digitalWrite(LED_PIN, LOW);
    delay(1);
    digitalWrite(SOUND_PIN, LOW);
    delay(4);
    digitalWrite(LED_PIN, HIGH);
}

void loop() {
    unsigned long now = millis();
    long delay = now - lastBeep;

    if (delay > currentDelay) {
        generateCurrentDelay();
        lastBeep = now;
        beep();
    }
    if (isNeedScan) {
        scanAsync();
    }
}



