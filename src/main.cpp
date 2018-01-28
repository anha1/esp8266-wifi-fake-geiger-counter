#include <Arduino.h>
#include "ESP8266WiFi.h"
#define SOUND_PIN 5

volatile float maxDelay = 600000;
volatile float currentDelay = 600000;
volatile boolean isNeedScan = false;

unsigned long lastBeep = 0;

void generateCurrentDelay() {
    currentDelay = random(1, maxDelay);
}

void onCompleteScan(int networksFound) {
    float strongestSignal = 10000;

    for(int i =0; i < networksFound; i++) {
        int currentSignal = abs(WiFi.RSSI(i));
        if (currentSignal < strongestSignal) {
            strongestSignal = currentSignal;
        }
        /*
        Serial.print("Network name: ");
        Serial.println(WiFi.SSID(i));
        Serial.print("Signal strength: ");
        Serial.println(WiFi.RSSI(i));
        */
    }
    maxDelay =  exp((strongestSignal - 15.) / 10.) * 5.;
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
    digitalWrite(SOUND_PIN, LOW);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    delay(1000);
 
    scanAsync(); 
}

void beep() {
    digitalWrite(SOUND_PIN, HIGH);
    delay(1);
    digitalWrite(SOUND_PIN, LOW);
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



