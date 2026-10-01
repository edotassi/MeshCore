#include "UITask.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#if defined(WITH_MQTT_BRIDGE) || defined(WITH_MQTT_WORMHOLE_BRIDGE)
#include <WiFi.h>
#endif

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

// 'meshcore', 128x13px
static const uint8_t meshcore_logo [] PROGMEM = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe, 
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc, 
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00, 
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00, 
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0, 
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00, 
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00, 
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8, 
};

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version, CommonCLICallbacks* callbacks) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _callbacks = callbacks;
  _display->turnOn();

  // strip off dash and commit hash by changing dash to null terminator
  // e.g: v1.2.3-abcdef -> v1.2.3
  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if(dash){
    *dash = 0;
  }

  // v1.2.3 (1 Jan 2025)
  snprintf(_version_info, sizeof(_version_info), "%s (%s)", version, build_date);
  free(version);
}

void UITask::renderCurrScreen() {
  char tmp[80];
  if (millis() < BOOT_SCREEN_MILLIS) { // boot screen
    // meshcore logo
    _display->setColor(UIColor::corp_blue);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // meshcore website
    const char* website = "https://meshcore.io";
    _display->setColor(UIColor::primary_txt);
    _display->setTextSize(1);
    uint16_t websiteWidth = _display->getTextWidth(website);
    _display->setCursor((_display->width() - websiteWidth) / 2, 22);
    _display->print(website);

    // version info
    _display->setTextSize(1);
    uint16_t versionWidth = _display->getTextWidth(_version_info);
    _display->setCursor((_display->width() - versionWidth) / 2, 35);
    _display->print(_version_info);

    // node type
    const char* node_type = "< Room Server >";
    uint16_t typeWidth = _display->getTextWidth(node_type);
    _display->setCursor((_display->width() - typeWidth) / 2, 48);
    _display->print(node_type);
  } else {  // home screen
    // node name
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(UIColor::primary_txt);
    _display->print(_node_prefs->node_name);

#if !defined(WITH_MQTT_BRIDGE) && !defined(WITH_MQTT_WORMHOLE_BRIDGE)
    // freq / sf
    _display->setCursor(0, 20);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    // bw / cr
    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);
#else
    // radio params, merged onto one line to make room for status rows below
    _display->setCursor(0, 10);
    sprintf(tmp, "%06.3f SF%d BW%03.1f CR%d", _node_prefs->freq, _node_prefs->sf, _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);

    // uptime + free heap
    _display->setCursor(0, 20);
    {
      uint32_t secs = millis() / 1000;
      uint32_t days = secs / 86400;
      uint32_t hours = (secs % 86400) / 3600;
      uint32_t mins = (secs % 3600) / 60;
      sprintf(tmp, "Up:%lud%luh%lum H:%luk", (unsigned long)days, (unsigned long)hours,
              (unsigned long)mins, (unsigned long)(ESP.getFreeHeap() / 1024));
    }
    _display->print(tmp);

    // WiFi status
    _display->setCursor(0, 30);
    if (_node_prefs->wifi_ssid[0] == 0) {
      sprintf(tmp, "WiFi: not configured");
    } else if (WiFi.status() == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      sprintf(tmp, "WiFi: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    } else {
      sprintf(tmp, "WiFi: connecting...");
    }
    _display->print(tmp);

#if defined(WITH_MQTT_BRIDGE) && defined(WITH_MQTT_WORMHOLE_BRIDGE)
    // both present: alternate rows 40/50 between MQTT observer status and wormhole
    // status every 5s, so both fit on the small screen without a page-switch button
    bool show_wormhole_page = _callbacks != nullptr && (millis() / 5000) % 2 == 1;
#elif defined(WITH_MQTT_WORMHOLE_BRIDGE)
    // wormhole only, no observer: always show it, nothing to alternate with
    bool show_wormhole_page = _callbacks != nullptr;
#else
    bool show_wormhole_page = false;
#endif

    if (show_wormhole_page) {
      // wormhole link status
      _display->setCursor(0, 40);
      bool wh_on = _callbacks->isWormholeRunning();
      bool wh_ok = _callbacks->isWormholeConnected();
      sprintf(tmp, "Wormhole:%s", !wh_on ? "off" : (wh_ok ? "OK" : "connecting"));
      _display->print(tmp);

      // packets relayed through the wormhole
      _display->setCursor(0, 50);
      sprintf(tmp, "WH Tx:%lu Rx:%lu", (unsigned long)_callbacks->getWormholeSentCount(),
              (unsigned long)_callbacks->getWormholeReceivedCount());
      _display->print(tmp);
    } else {
      // MQTT status + publish counters
      _display->setCursor(0, 40);
      if (_callbacks != nullptr) {
        bool mqtt_ok = _callbacks->isMqttConnected();
        sprintf(tmp, "MQTT:%s %lu/%lu", mqtt_ok ? "OK" : "off",
                (unsigned long)_callbacks->getMqttOkCount(), (unsigned long)_callbacks->getMqttFailCount());
      } else {
        sprintf(tmp, "MQTT: off");
      }
      _display->print(tmp);

      // packets observed by the bridge + connected room clients
      _display->setCursor(0, 50);
      if (_callbacks != nullptr) {
        int clients = _callbacks->getConnectedClientCount();
        if (clients >= 0) {
          sprintf(tmp, "Pkts:%lu Clients:%d", (unsigned long)_callbacks->getBridgePacketCount(), clients);
        } else {
          sprintf(tmp, "Pkts:%lu", (unsigned long)_callbacks->getBridgePacketCount());
        }
      } else {
        sprintf(tmp, "Pkts:0");
      }
      _display->print(tmp);
    }
#endif
  }
}

void UITask::loop() {
#ifdef PIN_USER_BTN
  if (millis() >= _next_read) {
    int btnState = digitalRead(PIN_USER_BTN);
    if (btnState != _prevBtnState) {
      if (btnState == USER_BTN_PRESSED) {  // pressed?
        if (_display->isOn()) {
          // TODO: any action ?
        } else {
          _display->turnOn();
        }
        _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
      }
      _prevBtnState = btnState;
    }
    _next_read = millis() + 200;  // 5 reads per second
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 1000;   // refresh every second
    }
    if (_node_prefs->screen_timeout_enabled && millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
