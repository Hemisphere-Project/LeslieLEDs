/*
 * Simple LED Strip Test for LeslieLEDs - M5Core Version
 * Tests GPIO 26 with SK6812 RGBW strip + DMX output
 * 
 * Visual Status via M5Core RGB LED (GPIO 10):
 * - Fast RED blink = Testing strip
 * - Slow GREEN blink = Strip test complete, continuous test running
 * - Fast BLUE blink = Waiting/heartbeat
 * 
 * DMX Output using LXESP32DMX (TX=13):
 * - 6 channels: R, G, B, W, Master, Reserved
 * - Mirrors LED strip colors
 * 
 * Upload with: pio run -e test_m5core -t upload
 */

#include <Arduino.h>
#include <FastLED.h>
#include <LXESP32DMX.h>

// Configuration for M5Core
#define LED_DATA_PIN 21             // M5Core GPIO 21 for LED strip (safe GPIO)
#define LED_COUNT 300               // Full 300 LED strip
#define LED_TYPE SK6812             // SK6812 RGBW
#define LED_COLOR_ORDER GRB         // Color order

// DMX Configuration - M5Module-DMX512 for Core
#define DMX_TX_PIN 13               // M5Module-DMX512 TX
#define DMX_RX_PIN 35               // M5Module-DMX512 RX (not used for output)
#define DMX_EN_PIN 12               // M5Module-DMX512 Enable pin (MUST be HIGH)
#define DMX_CHANNELS 512            // Standard DMX512
#define DMX_FIXTURE_START 1         // First fixture at channel 1

// For RGBW strips, we need 4 bytes per LED (R, G, B, W)
struct CRGBW {
  uint8_t g;
  uint8_t r;
  uint8_t b;
  uint8_t w;
};

CRGBW leds[LED_COUNT];

void setDMXRGBW(uint8_t r, uint8_t g, uint8_t b, uint8_t w, uint8_t master) {
  // LXESP32DMX: Use semaphore for thread safety
  if (xSemaphoreTake(ESP32DMX.lxDataLock, portMAX_DELAY) == pdTRUE) {
    ESP32DMX.setSlot(1, r);       // Channel 1: Red
    ESP32DMX.setSlot(2, g);       // Channel 2: Green
    ESP32DMX.setSlot(3, b);       // Channel 3: Blue
    ESP32DMX.setSlot(4, w);       // Channel 4: White
    ESP32DMX.setSlot(5, master);  // Channel 5: Master/Dimmer
    ESP32DMX.setSlot(6, 0);       // Channel 6: Unused
    xSemaphoreGive(ESP32DMX.lxDataLock);
  }
  
  // Read back and verify
  Serial.printf("    Set DMX - Ch1(R):%d Ch2(G):%d Ch3(B):%d Ch4(W):%d Ch5(M):%d\n", 
                ESP32DMX.getSlot(1), ESP32DMX.getSlot(2), ESP32DMX.getSlot(3), 
                ESP32DMX.getSlot(4), ESP32DMX.getSlot(5));
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=================================");
  Serial.println("LeslieLEDs - M5Core LED + DMX Test");
  Serial.println("GPIO 21 - SK6812 RGBW Strip");
  Serial.println("M5Module-DMX512: TX=13, RX=35, EN=12");
  Serial.println("=================================\n");
  
  Serial.printf("ESP32 CPU Freq: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Free heap: %d bytes\n\n", ESP.getFreeHeap());
  
  Serial.println("Starting LED strip and DMX test...");
  delay(1000);
  
  // Initialize DMX Enable Pin
  Serial.println("Setting up M5Module-DMX512 enable pin...");
  pinMode(DMX_EN_PIN, OUTPUT);
  digitalWrite(DMX_EN_PIN, HIGH);  // ENABLE the RS485 transceiver
  Serial.printf("  ✓ EN pin (GPIO%d) set HIGH\n", DMX_EN_PIN);
  delay(10);
  
  // Initialize DMX
  Serial.println("Initializing DMX on TX=13...");
  Serial.println("  Calling ESP32DMX.startOutput()...");
  ESP32DMX.startOutput(DMX_TX_PIN);
  delay(100);  // Give DMX time to start
  
  Serial.printf("  DMX initialized! Pin: %d\n", DMX_TX_PIN);
  Serial.printf("  DMX mode: %d (0=DMX_TASK_SEND)\n", ESP32DMX.rdmTaskMode());
  
  // Check if DMX is actually running
  if (ESP32DMX.rdmTaskMode() == 0) {
    Serial.println("  ✓ DMX is in OUTPUT mode");
  } else {
    Serial.println("  ✗ WARNING: DMX is NOT in output mode!");
  }
  
  // Set all DMX channels to 0 initially
  Serial.println("  Clearing DMX channels...");
  if (xSemaphoreTake(ESP32DMX.lxDataLock, portMAX_DELAY) == pdTRUE) {
    for (int i = 1; i <= 6; i++) {
      ESP32DMX.setSlot(i, 0);
    }
    xSemaphoreGive(ESP32DMX.lxDataLock);
  }
  Serial.println("  DMX channels cleared!");
  Serial.println();
  
  // Initialize external RGBW strip on GPIO 26
  // We treat it as raw LED data (4 bytes per pixel: G, R, B, W)
  Serial.println("Initializing RGBW strip on GPIO 26...");
  FastLED.addLeds<LED_TYPE, LED_DATA_PIN>((CRGB*)leds, LED_COUNT * 4 / 3);
  FastLED.setBrightness(77);  // 30% brightness (77/255 = ~30%)
  
  // Clear all LEDs
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 0, 0};
  }
  FastLED.show();
  
  // Test 1: ALL LEDs RED + DMX RED
  Serial.println("Test 1: All LEDs RED + DMX RED");
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 255, 0, 0};  // G, R, B, W
  }
  FastLED.show();
  setDMXRGBW(255, 0, 0, 0, 255);  // R, G, B, W, Master
  Serial.println("  DMX: R=255, G=0, B=0, W=0, Master=255");
  delay(3000);
  
  // Test 2: ALL LEDs GREEN + DMX GREEN
  Serial.println("Test 2: All LEDs GREEN + DMX GREEN");
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {255, 0, 0, 0};  // G, R, B, W
  }
  FastLED.show();
  setDMXRGBW(0, 255, 0, 0, 255);  // R, G, B, W, Master
  Serial.println("  DMX: R=0, G=255, B=0, W=0, Master=255");
  delay(3000);
  
  // Test 3: ALL LEDs BLUE + DMX BLUE
  Serial.println("Test 3: All LEDs BLUE + DMX BLUE");
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 255, 0};  // G, R, B, W
  }
  FastLED.show();
  setDMXRGBW(0, 0, 255, 0, 255);  // R, G, B, W, Master
  Serial.println("  DMX: R=0, G=0, B=255, W=0, Master=255");
  delay(3000);
  
  // Test 4: ALL LEDs WHITE (using W channel) + DMX WHITE
  Serial.println("Test 4: All LEDs WHITE (W channel) + DMX WHITE");
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 0, 255};  // G, R, B, W - pure white channel
  }
  FastLED.show();
  setDMXRGBW(0, 0, 0, 255, 255);  // R, G, B, W, Master
  Serial.println("  DMX: R=0, G=0, B=0, W=255, Master=255");
  delay(3000);
  
  // Test 5: Rainbow across all 300 LEDs (RGB only, no W) + DMX Rainbow cycle
  Serial.println("Test 5: Rainbow pattern + DMX cycle");
  for(int i = 0; i < LED_COUNT; i++) {
    uint8_t hue = (i * 256 / LED_COUNT);
    // Convert HSV to RGB manually
    CRGB rgb = CHSV(hue, 255, 255);
    leds[i] = {rgb.g, rgb.r, rgb.b, 0};  // No white channel
  }
  FastLED.show();
  // DMX will cycle in loop
  delay(5000);
  
  // Clear
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 0, 0};
  }
  FastLED.show();
  setDMXRGBW(0, 0, 0, 0, 0);
  
  Serial.println("Test complete! Starting continuous pattern...\n");
}

void loop() {
  // Continuous chase pattern across all 300 LEDs
  static unsigned long lastUpdate = 0;
  static int pos = 0;
  static uint8_t hue = 0;
  
  if (millis() - lastUpdate > 20) {  // ~50 FPS
    // Clear strip
    for(int i = 0; i < LED_COUNT; i++) {
      leds[i] = {0, 0, 0, 0};
    }
    
    // Draw 10-LED comet tail
    for(int i = 0; i < 10; i++) {
      int ledPos = (pos + i) % LED_COUNT;
      uint8_t brightness = 255 - (i * 25);  // Fade tail
      
      // Convert HSV to RGB for the comet
      CRGB rgb = CHSV(hue, 255, brightness);
      leds[ledPos] = {rgb.g, rgb.r, rgb.b, 0};  // G, R, B, W (no white)
    }
    
    FastLED.show();
    
    // Update DMX to match the head color
    CRGB dmxColor = CHSV(hue, 255, 255);
    setDMXRGBW(dmxColor.r, dmxColor.g, dmxColor.b, 0, 255);
    
    pos++;
    if(pos >= LED_COUNT) {
      pos = 0;
      hue += 16;  // Change color on each lap
      Serial.printf("Lap complete! Hue: %d\n", hue);
    }
    
    lastUpdate = millis();
  }
}
