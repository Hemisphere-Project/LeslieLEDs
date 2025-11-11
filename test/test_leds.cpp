/*
 * Simple LED Strip Test for LeslieLEDs
 * Tests GPIO 2 with SK6812 RGBW strip
 * 
 * Visual Status via Built-in LED (GPIO 35):
 * - Fast RED blink = Testing strip
 * - Slow GREEN blink = Strip test complete, continuous test running
 * - Fast BLUE blink = Waiting/heartbeat
 * 
 * Upload with: pio run -e test -t upload
 */

#include <Arduino.h>
#include <FastLED.h>

// Configuration - Try different pins to diagnose
#define LED_DATA_PIN 2              // AtomS3 G2 label = GPIO 2
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

// Also test the built-in LED on GPIO 35
#define BUILTIN_LED 35
CRGB builtInLed[1];

void setup() {
  // Initialize FastLED for built-in LED first (we know this works)
  FastLED.addLeds<WS2812, BUILTIN_LED, GRB>(builtInLed, 1);
  
  // Signal: Starting test - FAST RED BLINKS
  for(int i = 0; i < 10; i++) {
    builtInLed[0] = CRGB::Red;
    FastLED.show();
    delay(100);
    builtInLed[0] = CRGB::Black;
    FastLED.show();
    delay(100);
  }
  delay(1000);
  
  // Initialize external RGBW strip on GPIO 2
  // We treat it as raw LED data (4 bytes per pixel: G, R, B, W)
  FastLED.addLeds<LED_TYPE, LED_DATA_PIN>((CRGB*)leds, LED_COUNT * 4 / 3);
  FastLED.setBrightness(77);  // 30% brightness (77/255 = ~30%)
  
  // Clear all LEDs
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 0, 0};
  }
  FastLED.show();
  
  // Test 1: ALL LEDs RED
  builtInLed[0] = CRGB::Red;
  FastLED.show();
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 255, 0, 0};  // G, R, B, W
  }
  FastLED.show();
  delay(3000);
  
  // Test 2: ALL LEDs GREEN
  builtInLed[0] = CRGB::Green;
  FastLED.show();
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {255, 0, 0, 0};  // G, R, B, W
  }
  FastLED.show();
  delay(3000);
  
  // Test 3: ALL LEDs BLUE
  builtInLed[0] = CRGB::Blue;
  FastLED.show();
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 255, 0};  // G, R, B, W
  }
  FastLED.show();
  delay(3000);
  
  // Test 4: ALL LEDs WHITE (using W channel)
  builtInLed[0] = CRGB::White;
  FastLED.show();
  for(int i = 0; i < LED_COUNT; i++) {
    leds[i] = {0, 0, 0, 255};  // G, R, B, W - pure white channel
  }
  FastLED.show();
  delay(3000);
  
  // Test 5: Rainbow across all 300 LEDs (RGB only, no W)
  builtInLed[0] = CRGB(128, 0, 128);  // Purple
  FastLED.show();
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
  
  // Signal: Test complete - SLOW GREEN BLINKS
  for(int i = 0; i < 5; i++) {
    builtInLed[0] = CRGB::Green;
    FastLED.show();
    delay(300);
    builtInLed[0] = CRGB::Black;
    FastLED.show();
    delay(300);
  }
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
    
    // Built-in LED shows current hue
    builtInLed[0] = CHSV(hue, 255, 50);
    
    FastLED.show();
    
    pos++;
    if(pos >= LED_COUNT) {
      pos = 0;
      hue += 16;  // Change color on each lap
    }
    
    lastUpdate = millis();
  }
}
