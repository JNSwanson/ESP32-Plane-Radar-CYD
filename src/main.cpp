/**
 * Plane Radar — WiFi setup, then radar UI on the ESP32-2432S028R CYD display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
bool g_touch_down = false;
TaskHandle_t g_adsb_fetch_task = nullptr;
volatile bool g_adsb_fetch_requested = false;
volatile bool g_adsb_fetch_running = false;
volatile bool g_adsb_refresh_ready = false;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void handleTouch() {
  uint16_t x = 0;
  uint16_t y = 0;
  const bool down = tft.getTouch(&x, &y);
  if (!down) {
    g_touch_down = false;
    return;
  }
  if (g_touch_down) {
    return;
  }
  g_touch_down = true;

  const ui::RadarTouchAction action = ui::radarDisplayHandleTouch(
      static_cast<int>(x), static_cast<int>(y));
  if (action == ui::RadarTouchAction::kNone) {
    return;
  }

  if (action == ui::RadarTouchAction::kRangeChanged) {
    char range_label[12];
    ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
    Serial.printf("Touch zoom: %s (outer ~%.0f km)\n", range_label,
                  ui::radar::rangeCurrent().outer_km);
  } else {
    Serial.printf("Touch radar sweep: %s\n",
                  ui::radar::sweepEnabled() ? "on" : "off");
  }
  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

bool fetchAircraftNow() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  return services::adsb::fetchUpdate(services::location::lat(),
                                     services::location::lon(), fetch_km);
}

void fetchAircraftTask(void*) {
  Serial.printf("ADS-B worker running on core %d\n", xPortGetCoreID());
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    g_adsb_fetch_requested = false;
    if (WiFi.status() != WL_CONNECTED) {
      continue;
    }
    g_adsb_fetch_running = true;
    if (fetchAircraftNow()) {
      g_adsb_refresh_ready = true;
    }
    g_adsb_fetch_running = false;
  }
}

void startAdsbWorker() {
  if (xTaskCreatePinnedToCore(fetchAircraftTask, "adsb-fetch",
                              config::kAdsbTaskStackBytes, nullptr, 1,
                              &g_adsb_fetch_task,
                              config::kAdsbTaskCore) != pdPASS) {
    g_adsb_fetch_task = nullptr;
    Serial.println("adsb: failed to create core 0 worker; using main loop");
  }
}

void requestAircraftFetch() {
  if (g_adsb_fetch_task != nullptr) {
    if (!g_adsb_fetch_requested && !g_adsb_fetch_running) {
      g_adsb_fetch_requested = true;
      xTaskNotifyGive(g_adsb_fetch_task);
    }
    return;
  }

  // Safe fallback if the worker could not be allocated.
  if (fetchAircraftNow()) {
    ui::radarDisplayRefreshAircraft();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  // Networking runs on core 0; display, touch, portal, and sweep stay on the
  // Arduino loop task on core 1. Avoid calling the portal from both cores.
  services::adsb::setPollFn(nullptr);
  startAdsbWorker();

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();
  handleTouch();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (!g_adsb_fetch_requested && !g_adsb_fetch_running &&
               millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      requestAircraftFetch();
    }

    if (g_adsb_refresh_ready) {
      g_adsb_refresh_ready = false;
      ui::radarDisplayRefreshAircraft();
    }
    if (g_radar_visible) {
      ui::radarDisplayAnimate();
    }
  }

  delay(1);
}
