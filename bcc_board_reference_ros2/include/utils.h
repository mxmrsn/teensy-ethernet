#ifndef UTILS_H
#define UTILS_H

#include "pin_defs.h"
#include "Adafruit_NeoPixel.h"
#include "Adafruit_MCP4728.h"

// ADAFRUIT NEOPIXEL LED STRIP
static Adafruit_NeoPixel strip = Adafruit_NeoPixel(N_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

enum StripColorState {
    COLOR_OFF,
    COLOR_RED,
    COLOR_BLUE,
    COLOR_YELLOW
};
static StripColorState currentColorState = COLOR_OFF;

inline void setStripRed() {
    if (currentColorState != COLOR_RED) {
        strip.clear();
        for (int ii=0; ii<strip.numPixels(); ii++) {
            strip.setPixelColor(ii, strip.Color(100,0,0));
        }
        strip.show();
    }
}
inline void setStripBlue() {
    if (currentColorState != COLOR_BLUE) {
        strip.clear();
        for (int ii=0; ii<strip.numPixels(); ii++) {
            strip.setPixelColor(ii, strip.Color(0,0,100));
        }
        strip.show();
    }
}
inline void setStripYellow() {
    if (currentColorState != COLOR_YELLOW) {
        for (int ii=0; ii<strip.numPixels(); ii++) {
            strip.setPixelColor(ii, strip.Color(50,50,0));
        }
        strip.show();
    }
}
inline void turnOffStrip() {
    if (currentColorState != COLOR_OFF) {
        strip.clear();
        strip.show();
        currentColorState = COLOR_OFF;
    }
}

// ADAFRUIT MCP4728 DAC
static Adafruit_MCP4728 dac;

#endif // UTILS_H