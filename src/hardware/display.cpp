#include "hardware/display.h"

#include "hardware/display_font.h"
#include "config.h"

LGFX tft;

void displayInit() {
  tft.init();
  // ILI9341 is 240x320 natively; rotation 1 gives 320x240 landscape.
  tft.setRotation(1);
  tft.fillScreen(config::kColorBlack);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}
