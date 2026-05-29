#include "wifi_config.h"

#if defined(WIFI_CONFIG_PORTAL) && defined(ESP32)

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "current_settings.h"

#ifndef WIFI_CONFIG_AP_SSID
#define WIFI_CONFIG_AP_SSID "MaxDuino"
#endif

#if defined(FTP_SERVER_ENABLED)
  #ifndef FTP_SERVER_USER
  #define FTP_SERVER_USER "maxduino"
  #endif

  #ifndef FTP_SERVER_PASSWORD
  #define FTP_SERVER_PASSWORD "maxduino"
  #endif

  #ifndef FTP_CONTROL_PORT
  #define FTP_CONTROL_PORT 21
  #endif
#endif

static WebServer server(80);
static bool serverStarted = false;
static char savedSsid[33];
static char savedPassword[65];

static void html_header(String &html, const char *title)
{
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>");
  html += title;
  html += F("</title><style>");
  html += F("body{font-family:Arial,sans-serif;margin:24px;max-width:560px;background:#f7f7f7;color:#111}");
  html += F("main{background:#fff;padding:18px;border:1px solid #ccc}");
  html += F("label{display:block;margin-top:12px;font-weight:bold}");
  html += F("input{box-sizing:border-box;width:100%;padding:9px;margin-top:4px}");
  html += F("button,a.button{display:inline-block;margin-top:16px;padding:10px 14px;background:#111;color:#fff;text-decoration:none;border:0}");
  html += F(".muted{color:#555}.ok{color:#05620f}.bad{color:#9b1111}");
  html += F("</style></head><body><main>");
  html += F("<h1>");
  html += title;
  html += F("</h1>");
}

static void html_footer(String &html)
{
  html += F("</main></body></html>");
}

static bool nvs_ready()
{
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if (nvs_flash_erase() != ESP_OK) return false;
    err = nvs_flash_init();
  }
  return err == ESP_OK;
}

static void load_wifi_credentials()
{
  savedSsid[0] = 0;
  savedPassword[0] = 0;
  if (!nvs_ready()) return;

  nvs_handle_t handle;
  if (nvs_open("wifi_cfg", NVS_READONLY, &handle) != ESP_OK) return;

  size_t len = sizeof(savedSsid);
  if (nvs_get_str(handle, "ssid", savedSsid, &len) != ESP_OK) {
    savedSsid[0] = 0;
  }

  len = sizeof(savedPassword);
  if (nvs_get_str(handle, "pass", savedPassword, &len) != ESP_OK) {
    savedPassword[0] = 0;
  }

  nvs_close(handle);
}

static bool save_wifi_credentials(const char *ssid, const char *password)
{
  if (!nvs_ready()) return false;

  nvs_handle_t handle;
  if (nvs_open("wifi_cfg", NVS_READWRITE, &handle) != ESP_OK) return false;

  esp_err_t err = nvs_set_str(handle, "ssid", ssid);
  if (err == ESP_OK) err = nvs_set_str(handle, "pass", password ? password : "");
  if (err == ESP_OK) err = nvs_commit(handle);

  nvs_close(handle);

  if (err == ESP_OK) {
    strncpy(savedSsid, ssid, sizeof(savedSsid) - 1);
    savedSsid[sizeof(savedSsid) - 1] = 0;
    strncpy(savedPassword, password ? password : "", sizeof(savedPassword) - 1);
    savedPassword[sizeof(savedPassword) - 1] = 0;
    return true;
  }
  return false;
}

static void clear_wifi_credentials()
{
  savedSsid[0] = 0;
  savedPassword[0] = 0;
  if (!nvs_ready()) return;

  nvs_handle_t handle;
  if (nvs_open("wifi_cfg", NVS_READWRITE, &handle) != ESP_OK) return;
  nvs_erase_all(handle);
  nvs_commit(handle);
  nvs_close(handle);
}

static const char *wifi_status_text(wl_status_t status)
{
  switch(status) {
    case WL_CONNECTED: return "connected";
    case WL_NO_SSID_AVAIL: return "network not found";
    case WL_CONNECT_FAILED: return "connect failed";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED: return "disconnected";
    default: return "connecting";
  }
}

static void render_home()
{
  String html;
  html.reserve(4000);
  html_header(html, "MaxDuino WiFi");

  html += F("<p>Connect to the open <strong>");
  html += WIFI_CONFIG_AP_SSID;
  html += F("</strong> WiFi network and open <strong>http://192.168.4.1/</strong>.</p>");

  html += F("<h2>Status</h2><p>Config AP: <strong>");
  html += WiFi.softAPIP().toString();
  html += F("</strong></p>");

  html += F("<p>Client WiFi: <strong class='");
  html += (WiFi.status() == WL_CONNECTED) ? F("ok'>") : F("bad'>");
  html += wifi_status_text(WiFi.status());
  html += F("</strong></p>");

  if (WiFi.status() == WL_CONNECTED) {
    html += F("<p>Network: <strong>");
    html += WiFi.SSID();
    html += F("</strong><br>IP address: <strong>");
    html += WiFi.localIP().toString();
    html += F("</strong></p>");
  } else if (savedSsid[0]) {
    html += F("<p class='muted'>Saved network: ");
    html += savedSsid;
    html += F("</p>");
  }

  #if defined(FTP_SERVER_ENABLED)
  html += F("<h2>SD Card FTP</h2><p>AP address: <strong>ftp://");
  html += WiFi.softAPIP().toString();
  html += F(":");
  html += FTP_CONTROL_PORT;
  html += F("/</strong></p>");

  if (WiFi.status() == WL_CONNECTED) {
    html += F("<p>LAN address: <strong>ftp://");
    html += WiFi.localIP().toString();
    html += F(":");
    html += FTP_CONTROL_PORT;
    html += F("/</strong></p>");
  }

  html += F("<p>Username: <strong>");
  html += FTP_SERVER_USER;
  html += F("</strong><br>Password: <strong>");
  html += FTP_SERVER_PASSWORD;
  html += F("</strong></p>");
  #endif

  #if defined(USE_PCM5120_I2S)
  html += F("<h2>Audio</h2>");
  html += F("<form method='post' action='/volume'>");
  html += F("<label for='volume'>Default volume</label>");
  html += F("<input id='volume' name='volume' type='number' min='");
  html += String(PCM5120_I2S_VOLUME_MIN);
  html += F("' max='");
  html += String(PCM5120_I2S_VOLUME_MAX);
  html += F("' step='");
  html += String(PCM5120_I2S_VOLUME_STEP);
  html += F("' value='");
  html += String(pcm5120I2sAmplitude);
  html += F("' required>");
  html += F("<button type='submit'>Save Volume</button>");
  html += F("</form>");
  html += F("<p class='muted'>Saved value is used immediately and after the next power on.</p>");
  #endif

  html += F("<h2>Configure Client WiFi</h2>");
  html += F("<form method='post' action='/save'>");
  html += F("<label for='ssid'>Network name</label>");
  html += F("<input id='ssid' name='ssid' maxlength='32' value='");
  html += savedSsid;
  html += F("' required>");
  html += F("<label for='pass'>Password</label>");
  html += F("<input id='pass' name='pass' maxlength='64' type='password'>");
  html += F("<button type='submit'>Connect and Save</button>");
  html += F("</form>");

  if (savedSsid[0]) {
    html += F("<p><a class='button' href='/forget'>Forget Saved WiFi</a></p>");
  }

  html_footer(html);
  server.send(200, "text/html", html);
}

static void render_message(const char *title, const char *message, bool ok)
{
  String html;
  html.reserve(1200);
  html_header(html, title);
  html += F("<p class='");
  html += ok ? F("ok'>") : F("bad'>");
  html += message;
  html += F("</p><p><a class='button' href='/'>Back</a></p>");
  html_footer(html);
  server.send(200, "text/html", html);
}

static void handle_save()
{
  if (!server.hasArg("ssid")) {
    render_message("WiFi Not Saved", "Missing network name.", false);
    return;
  }

  String ssid = server.arg("ssid");
  String password = server.hasArg("pass") ? server.arg("pass") : "";
  ssid.trim();

  if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64) {
    render_message("WiFi Not Saved", "The network name or password is not valid.", false);
    return;
  }

  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());

  const unsigned long startMillis = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMillis < 20000UL) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    render_message("Connection Failed", "The device could not connect. Check the network name and password. Settings were not saved.", false);
    return;
  }

  if (!save_wifi_credentials(ssid.c_str(), password.c_str())) {
    render_message("WiFi Connected", "Connected, but saving settings failed.", false);
    return;
  }

  render_message("WiFi Saved", "Connected successfully. These settings will be used on the next power on.", true);
}

#if defined(USE_PCM5120_I2S)
static bool parse_volume_value(String value, word &volume)
{
  value.trim();
  if (value.length() == 0) return false;

  for (unsigned int i = 0; i < value.length(); i++) {
    if (value[i] < '0' || value[i] > '9') return false;
  }

  unsigned long parsed = value.toInt();
  if (parsed < PCM5120_I2S_VOLUME_MIN || parsed > PCM5120_I2S_VOLUME_MAX) return false;
  if ((parsed - PCM5120_I2S_VOLUME_MIN) % PCM5120_I2S_VOLUME_STEP != 0) return false;

  volume = clampPcm5120I2sAmplitude(parsed);
  return true;
}

static void handle_volume()
{
  word volume;
  if (!server.hasArg("volume") || !parse_volume_value(server.arg("volume"), volume)) {
    render_message("Volume Not Saved", "The volume value is not valid.", false);
    return;
  }

  if (!savePersistentPcm5120I2sAmplitude(volume)) {
    render_message("Volume Not Saved", "The device could not save the volume setting.", false);
    return;
  }

  render_message("Volume Saved", "Default volume was saved and will be used on the next power on.", true);
}
#endif

static void handle_forget()
{
  clear_wifi_credentials();
  WiFi.disconnect(false, false);
  render_message("WiFi Forgotten", "Saved client WiFi settings were removed.", true);
}

void wifi_config_begin()
{
  load_wifi_credentials();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_CONFIG_AP_SSID);

  if (savedSsid[0]) {
    WiFi.begin(savedSsid, savedPassword);
  }

  server.on("/", HTTP_GET, render_home);
  server.on("/save", HTTP_POST, handle_save);
  #if defined(USE_PCM5120_I2S)
  server.on("/volume", HTTP_POST, handle_volume);
  #endif
  server.on("/forget", HTTP_GET, handle_forget);
  server.onNotFound(render_home);
  server.begin();
  serverStarted = true;
}

void wifi_config_loop()
{
  if (serverStarted) {
    server.handleClient();
  }
}

#endif // defined(WIFI_CONFIG_PORTAL) && defined(ESP32)
