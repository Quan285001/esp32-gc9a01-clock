// ---------------------------------------------------------------------------
//  Persistent settings: WiFi, location, units. Kept in NVS so a clock can be
//  set up from a phone and never needs its firmware rebuilt.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

struct Settings {
    String ssid, pass;
    String locQuery;      // what was typed: a ZIP / postcode or a town
    String locName;       // resolved by the geocoder, e.g. "Orlando, US"
    String tzName;        // IANA zone, e.g. America/New_York
    float  lat = 0, lon = 0;
    bool   hasLoc = false;
    bool   useF = true;
    int    utcOffset = 0; // seconds east of UTC, as last reported by the weather feed
    bool   hasOffset = false;

    // --- CÁC BIẾN MỚI THÊM VÀO ---
    uint32_t rotateMs = 60000;      // Thời gian chuyển mặt đồng hồ (mặc định 60 giây)
    uint32_t sleepTimeout = 15000;  // Thời gian chờ để đi ngủ (mặc định 15 giây)


    bool hasWifi() const { return ssid.length() > 0; }
};

extern Settings settings;

void settingsLoad();
void settingsSave();
void settingsClear();
