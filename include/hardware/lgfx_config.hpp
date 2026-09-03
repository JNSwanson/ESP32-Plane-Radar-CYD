#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

/** LovyanGFX device: ILI9341 on the ESP32-2432S028R CYD. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_ILI9341 _panel;
  lgfx::Light_PWM _backlight;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.freq_read = 16000000;
      cfg.spi_mode = 0;
      cfg.spi_3wire = false;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = static_cast<int>(config::kDisplayPinMiso);
      cfg.pin_dc = static_cast<int>(config::kDisplayPinDc);
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = static_cast<int>(config::kDisplayPinCs);
      cfg.pin_rst = static_cast<int>(config::kDisplayPinRst);
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      // The common 2432S028R ILI9341 wiring needs a rotation offset.
      cfg.offset_rotation = 2;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _backlight.config();
      cfg.pin_bl = static_cast<int>(config::kDisplayPinBl);
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _backlight.config(cfg);
      _panel.setLight(&_backlight);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 3700;
      cfg.y_max = 200;
      cfg.pin_int = -1;
      cfg.bus_shared = false;
      cfg.spi_host = -1;  // Software SPI; touch has separate CYD pins.
      cfg.pin_sclk = static_cast<int>(config::kTouchPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kTouchPinMosi);
      cfg.pin_miso = static_cast<int>(config::kTouchPinMiso);
      cfg.pin_cs = static_cast<int>(config::kTouchPinCs);
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
