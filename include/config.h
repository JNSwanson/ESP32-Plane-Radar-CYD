#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (ESP32-2432S028R CYD, active LOW) ---
// CYD BOOT button is connected to GPIO0.
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: ESP32-2432S028R CYD, ILI9341 2.8" 240×320 (landscape 320×240) ---
constexpr gpio_num_t kDisplayPinRst = static_cast<gpio_num_t>(-1);
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_15;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_2;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_13;  // display SDA / MOSI
constexpr gpio_num_t kDisplayPinMiso = GPIO_NUM_12;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_14;
constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_21;   // backlight, active HIGH

// --- Touch: ESP32-2432S028R CYD, XPT2046 on separate software SPI ---
constexpr gpio_num_t kTouchPinCs = GPIO_NUM_33;
constexpr gpio_num_t kTouchPinMosi = GPIO_NUM_32;
constexpr gpio_num_t kTouchPinMiso = GPIO_NUM_39;
constexpr gpio_num_t kTouchPinSclk = GPIO_NUM_25;

constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// Standard CYD ILI9341 is BGR; LovyanGFX rgb_order=false selects BGR.
constexpr bool kDisplayInvert = false;
constexpr bool kDisplayRgbOrder = false;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 3000;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- Animated radar sweep ---
/** Initial state used until changed by radar touch. */
constexpr bool kRadarSweepDefaultOn = false;
/** ADS-B/TLS worker stack and core (CYD ESP32-WROOM-32 is dual-core). */
constexpr uint32_t kAdsbTaskStackBytes = 10240;
constexpr int kAdsbTaskCore = 0;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
