/*
 * Simple LED Strip Test for LeslieLEDs - M5Core Version
 * Tests GPIO 26 with SK6812 RGBW strip
 * 
 * Visual Status via M5Core RGB LED (GPIO 10):
 * - Fast RED blink = Testing strip
 * - Slow GREEN blink = Strip test complete, continuous test running
 * - Fast BLUE blink = Waiting/heartbeat
 * 
 * Upload with: pio run -e test-m5core -t upload
 */

#include <Arduino.h>
#include <FastLED.h>

// Configuration for M5Core
#define LED_DATA_PIN 26             // M5Core GPIO 26 for LED strip (safe GPIO)
#define LED_COUNT 300               // Full 300 LED strip
#define LED_TYPE SK6812             // SK6812 RGBW
#define LED_COLOR_ORDER GRB         // Color order

// For RGBW strips, we need 4 bytes per LED (R, G, B, W)
struct CRGBW {
  uint8_t g;
  uint8_t r;
  uint8_t b;
  uint8_t w;
};

CRGBW leds[LED_COUNT];

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=================================");
  Serial.println("LeslieLEDs - M5Core LED Test");
  Serial.println("GPIO 26 - SK6812 RGBW Strip");
  Serial.println("=================================\n");
  
  Serial.println("Starting LED strip test...");
  delay(1000);
  
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
  
  // Test 1: ALL LEDs RED
  Serial.println("Test 1: All LEDs RED");
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 255, 0, 0};  // G, R, B, W
  }
  FastLED.show();
  delay(3000);
  
  // Test 2: ALL LEDs GREEN
  Serial.println("Test 2: All LEDs GREEN");
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {255, 0, 0, 0};  // G, R, B, W
  }
  FastLED.show();
  delay(3000);
  
  // Test 3: ALL LEDs BLUE
  Serial.println("Test 3: All LEDs BLUE");
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 255, 0};  // G, R, B, W
  }
  FastLED.show();
  delay(3000);
  
  // Test 4: ALL LEDs WHITE (using W channel)
  Serial.println("Test 4: All LEDs WHITE (W channel)");
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 0, 255};  // G, R, B, W - pure white channel
  }
  FastLED.show();
  delay(3000);
  
  // Test 5: Rainbow across all 300 LEDs (RGB only, no W)
  Serial.println("Test 5: Rainbow pattern");
  for(int i = 0; i < LED_COUNT; i++) {
    uint8_t hue = (i * 256 / LED_COUNT);
    // Convert HSV to RGB manually
    CRGB rgb = CHSV(hue, 255, 255);
    leds[i] = {rgb.g, rgb.r, rgb.b, 0};  // No white channel
  }
  FastLED.show();
  delay(5000);
  
  // Clear
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 0, 0};
  }
  FastLED.show();
  
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
    
    pos++;
    if(pos >= LED_COUNT) {
      pos = 0;
      hue += 16;  // Change color on each lap
      Serial.printf("Lap complete! Hue: %d\n", hue);
    }
    
    lastUpdate = millis();
  }
}
