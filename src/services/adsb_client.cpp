#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[2][kMaxAircraft];
size_t s_aircraft_count[2] = {0, 0};
volatile uint8_t s_active_aircraft = 0;
portMUX_TYPE s_aircraft_mux = portMUX_INITIALIZER_UNLOCKED;
PollFn s_poll_fn = nullptr;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

bool readResponseBodyWithPoll(HTTPClient& http, String& payload) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  if (content_length > 0) {
    payload.reserve(static_cast<unsigned>(content_length + 1));
  }

  const unsigned long deadline = millis() + kRequestTimeoutMs;

  auto beforeDeadline = [&]() {
    return static_cast<long>(millis() - deadline) < 0;
  };

  auto waitForData = [&]() {
    while (beforeDeadline()) {
      pollNetwork();
      if (stream->available() > 0) {
        return true;
      }
      if (!http.connected()) {
        return false;
      }
      delay(1);
    }
    return false;
  };

  auto readByte = [&](uint8_t* out) {
    if (!waitForData()) {
      return false;
    }
    const int value = stream->read();
    if (value < 0) {
      return false;
    }
    *out = static_cast<uint8_t>(value);
    return true;
  };

  auto readLine = [&](String& line) {
    line = "";
    while (beforeDeadline()) {
      uint8_t c = 0;
      if (!readByte(&c)) {
        return false;
      }
      if (c == '\r') {
        uint8_t lf = 0;
        return readByte(&lf) && lf == '\n';
      }
      line += static_cast<char>(c);
      // Chunk-size lines are tiny; cap this to reject malformed responses.
      if (line.length() > 32) {
        return false;
      }
    }
    return false;
  };

  auto readBodyBytes = [&](unsigned long count) {
    uint8_t buffer[512];
    unsigned long remaining = count;
    while (remaining > 0 && beforeDeadline()) {
      if (!waitForData()) {
        return false;
      }
      int to_read = stream->available();
      if (to_read > static_cast<int>(sizeof(buffer))) {
        to_read = static_cast<int>(sizeof(buffer));
      }
      if (static_cast<unsigned long>(to_read) > remaining) {
        to_read = static_cast<int>(remaining);
      }
      const int read_bytes = stream->readBytes(buffer, to_read);
      if (read_bytes <= 0) {
        return false;
      }
      payload.concat(reinterpret_cast<const char*>(buffer),
                     static_cast<unsigned>(read_bytes));
      remaining -= static_cast<unsigned long>(read_bytes);
    }
    return remaining == 0;
  };

  // Fixed-length responses contain JSON directly.
  if (content_length > 0) {
    return readBodyBytes(static_cast<unsigned long>(content_length));
  }

  // Cloudflare currently serves adsb.fi responses with chunked transfer
  // encoding. HTTPClient exposes the raw chunk framing through getStreamPtr(),
  // so decode each chunk before passing the payload to ArduinoJson.
  while (beforeDeadline()) {
    String chunk_size_line;
    if (!readLine(chunk_size_line)) {
      return false;
    }

    const int semicolon = chunk_size_line.indexOf(';');
    if (semicolon >= 0) {
      chunk_size_line = chunk_size_line.substring(0, semicolon);
    }
    chunk_size_line.trim();
    if (chunk_size_line.length() == 0) {
      return false;
    }

    char* end = nullptr;
    const unsigned long chunk_size =
        strtoul(chunk_size_line.c_str(), &end, 16);
    if (end == chunk_size_line.c_str() || *end != '\0') {
      return false;
    }

    if (chunk_size == 0) {
      // Consume optional trailer headers through their terminating blank line.
      while (true) {
        String trailer;
        if (!readLine(trailer)) {
          return false;
        }
        if (trailer.length() == 0) {
          return true;
        }
      }
    }

    if (!readBodyBytes(chunk_size)) {
      return false;
    }

    uint8_t cr = 0;
    uint8_t lf = 0;
    if (!readByte(&cr) || !readByte(&lf) || cr != '\r' || lf != '\n') {
      return false;
    }
  }

  return false;
}

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() {
  portENTER_CRITICAL(&s_aircraft_mux);
  const size_t count = s_aircraft_count[s_active_aircraft];
  portEXIT_CRITICAL(&s_aircraft_mux);
  return count;
}

const Aircraft* aircraftList() { return s_aircraft[s_active_aircraft]; }

size_t copyAircraft(Aircraft* out, size_t max_count) {
  if (out == nullptr || max_count == 0) {
    return 0;
  }
  portENTER_CRITICAL(&s_aircraft_mux);
  const uint8_t active = s_active_aircraft;
  const size_t count =
      s_aircraft_count[active] < max_count ? s_aircraft_count[active] : max_count;
  memcpy(out, s_aircraft[active], count * sizeof(Aircraft));
  portEXIT_CRITICAL(&s_aircraft_mux);
  return count;
}

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  portENTER_CRITICAL(&s_aircraft_mux);
  const uint8_t update_index = static_cast<uint8_t>(1U - s_active_aircraft);
  portEXIT_CRITICAL(&s_aircraft_mux);
  Aircraft* updated = s_aircraft[update_index];
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  http.setTimeout(kRequestTimeoutMs);
  const int code = performGetWithPoll(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    http.end();
    return false;
  }

  String payload;
  if (!readResponseBodyWithPoll(http, payload)) {
    Serial.println("adsb: empty response");
    http.end();
    return false;
  }
  http.end();

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    portENTER_CRITICAL(&s_aircraft_mux);
    s_aircraft_count[update_index] = 0;
    s_active_aircraft = update_index;
    portEXIT_CRITICAL(&s_aircraft_mux);
    Serial.println("adsb: response has no ac array");
    return true;
  }

  size_t n = 0;
  size_t missing_position = 0;
  size_t ground_filtered = 0;
  for (JsonObject plane : ac) {
    if (n >= kMaxAircraft) {
      break;
    }
    float lat = 0.0f;
    float lon = 0.0f;
    if (!readJsonFloat(plane, "lat", &lat) ||
        !readJsonFloat(plane, "lon", &lon)) {
      ++missing_position;
      continue;
    }
    if (isOnGround(plane) && !config::kAdsbShowGroundAircraft) {
      ++ground_filtered;
      continue;
    }

    updated[n].lat = lat;
    updated[n].lon = lon;
    updated[n].nose_deg = pickNoseHeading(plane);
    updated[n].track_deg = pickTrackHeading(plane);
    updated[n].gs_knots = pickGroundSpeed(plane);
    fillTagFields(&updated[n], plane);
    ++n;
  }

  portENTER_CRITICAL(&s_aircraft_mux);
  s_aircraft_count[update_index] = n;
  s_active_aircraft = update_index;
  portEXIT_CRITICAL(&s_aircraft_mux);
  Serial.printf(
      "adsb: %u aircraft (API %u, no position %u, ground filtered %u) "
      "at %.6f, %.6f radius %.1f km\n",
      static_cast<unsigned>(n), static_cast<unsigned>(ac.size()),
      static_cast<unsigned>(missing_position),
      static_cast<unsigned>(ground_filtered), center_lat, center_lon,
      fetch_radius_km);
  return true;
}

}  // namespace services::adsb
