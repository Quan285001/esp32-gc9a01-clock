// ---------------------------------------------------------------------------
//  NTP clock + weather  -  ESP32-S3 / ESP32-C3 Super Mini + GC9A01 round LCD,
//                          and the Waveshare ESP32-C6 1.43" AMOLED
//
//  This file owns WiFi, NTP, the weather fetch, the face rotation and touch.
//  The display itself is behind screen.h, with one back end per board
//  family. The visual design lives in faces/. Settings (WiFi, location,
//  units) live in NVS and are entered through the captive setup hotspot in
//  portal.cpp, so nothing has to be compiled in.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "face.h"
#include "screen.h"
#include "settings.h"
#include "portal.h"
#include "tz.h"
#include "anime_frames.h"

// --- KHAI BÁO NGUYÊN MẪU HÀM ---
void drawMenuScreen();
void playAnimeGif();
void handleBootButtonClicks();
void goToSleep();

#ifdef AMOLED_C6
#include "c6/touch.h"
#include "c6/audio.h"
#include "c6/alarm_ui.h"
#include "c6/rtc.h"
#include "c6/imu.h"
#include "c6/supervisor.h"
#include "alarms.h"

#define SUP(p)      supervisor::phase(supervisor::p)
#define SUP_ALIVE() supervisor::alive()
#define SUP_DREW()  supervisor::drew()
#else
#define SUP(p)      ((void)0)
#define SUP_ALIVE() ((void)0)
#define SUP_DREW()  ((void)0)
#endif

// ----------------------------- compiled-in defaults ------------------------
#if __has_include("config.h")
#include "config.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef WX_LOCATION
#define WX_LOCATION ""
#endif
#ifndef WX_UNITS
#define WX_UNITS "fahrenheit"
#endif
#ifdef TZ_INFO
static const char *CFG_TZ = TZ_INFO;
#else
static const char *CFG_TZ = nullptr;
#endif

static const uint32_t WX_PERIOD_MS = 10UL * 60UL * 1000UL;  
static const uint32_t WX_RETRY_MS  = 30UL * 1000UL;         
static const uint32_t PORTAL_AFTER_DOWN_MS = 60UL * 1000UL;

// --- Biến quản lý chống dội phím và thời gian hoạt động ---
static uint32_t lastDebounceTime = 0;
static uint32_t lastActivityTime = 0;

// --- CẤU HÌNH MENU DÙNG NÚT BOOT ---
enum MenuState {
    STATE_CLOCK,    
    STATE_MENU      
};

static MenuState currentAppState = STATE_CLOCK;
static int selectedMenuItem = 0; 
static const int TOTAL_MENU_ITEMS = 5;

const char* menuLabels[TOTAL_MENU_ITEMS] = {
    "1. Home screen",
    "2. Change watch face",
    "3. Turn on WiFi Hotspot",
    "4. Play Anime GIF",
    "5. Go to Sleep"
};

// ---- face rotation --------------------------------------------------------
#define FF_CAT_(a, b) a##b
#define FF_CAT(a, b)  FF_CAT_(a, b)
#if defined(FORCE_FACE) && FF_CAT(FORCE_FACE, 1) == 1
#error "FORCE_FACE is empty: set PREVIEW_FACE=FACE_<NAME> before building a preview env"
#endif

static const FaceVTable *const ROTATION[] = {
#if defined(FORCE_FACE)
    &FORCE_FACE,
#elif defined(FORCE_DAY) || defined(FORCE_NIGHT)
    &FACE_RETRO,
#else
    &FACE_RETRO, &FACE_DOTMATRIX, &FACE_PULSAR, &FACE_PCB,
    &FACE_CASIO, &FACE_MOSAIC, &FACE_DEFAULT, &FACE_CLASSIC, &FACE_MODERN,
    &FACE_PANEL, &FACE_DELOREAN, &FACE_CALIFORNIA, &FACE_OUTRUN,
    &FACE_NIXIE, 
#endif
};
static const int      ROTATION_N = sizeof(ROTATION) / sizeof(ROTATION[0]);
static const uint32_t ROTATE_MS  = 60UL * 1000UL;   
static const bool     ROTATE_RANDOM = true;

const FaceVTable *activeFace = ROTATION[0];
static int      faceIdx      = 0;
static uint32_t faceSince    = 0;

static int firstFaceIdx()
{
    return (ROTATE_RANDOM && ROTATION_N > 1) ? (int)(esp_random() % ROTATION_N) : 0;
}
static int nextFaceIdx(int cur)
{
    if (ROTATION_N < 2)  return 0;
    if (!ROTATE_RANDOM)  return (cur + 1) % ROTATION_N;
    return (cur + 1 + (int)(esp_random() % (ROTATION_N - 1))) % ROTATION_N;
}
static int      lastSec      = -1;

bool     timeValid = false;
uint16_t statusCol = 0xFD20;           
static uint32_t lastSyncCheck = 0;

volatile bool wxValid = false;
volatile int  wxTempF = 0;
volatile int  wxCode  = -1;
volatile bool wxIsDay = true;
volatile int  wxErr   = 0;    
volatile int  wxSunrise = -1; 
volatile int  wxSunset  = -1;
bool          wxUseF    = true;

static int isoToMinutes(const char *iso)
{
    if (!iso) return -1;
    const char *t = strchr(iso, 'T');
    if (!t || strlen(t) < 6) return -1;
    if (!isdigit((int)t[1]) || !isdigit((int)t[2]) ||
        !isdigit((int)t[4]) || !isdigit((int)t[5])) return -1;
    int hh = (t[1] - '0') * 10 + (t[2] - '0');
    int mm = (t[4] - '0') * 10 + (t[5] - '0');
    if (hh > 23 || mm > 59) return -1;
    return hh * 60 + mm;
}

static String urlEncode(const String &s)
{
    String o;
    for (char c : s) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') o += c;
        else if (c == ' ') o += '+';
        else { char b[4]; snprintf(b, sizeof b, "%%%02X", (unsigned char)c); o += b; }
    }
    return o;
}

static bool resolveLocation()
{
    if (settings.hasLoc) return true;
    if (!settings.locQuery.length() || WiFi.status() != WL_CONNECTED) return false;

    String url = "http://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name="
                 + urlEncode(settings.locQuery);
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    if (!http.begin(client, url)) return false;

    bool ok = false;
    if (http.GET() == HTTP_CODE_OK) {
        JsonDocument filter;
        JsonObject f = filter["results"][0].to<JsonObject>();
        f["latitude"] = true; f["longitude"] = true; f["timezone"] = true;
        f["name"] = true;     f["country_code"] = true;
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter))) {
            JsonObject r = doc["results"][0];
            if (!r.isNull()) {
                settings.lat     = r["latitude"]  | 0.0f;
                settings.lon     = r["longitude"] | 0.0f;
                settings.tzName  = (const char *)(r["timezone"] | "");
                settings.locName = String((const char *)(r["name"] | "")) + ", "
                                 + (const char *)(r["country_code"] | "");
                settings.hasLoc  = true;
                settingsSave();
                ok = true;
            }
        }
    }
    http.end();
    return ok;
}

static volatile bool tzPending = false;
volatile uint32_t diagWx = 0;
static char tzStaged[64] = "";

static void weatherTask(void *)
{
    for (;;) {
        bool ok = false;
        diagWx++;

        if (WiFi.status() != WL_CONNECTED) {
            wxErr = 1;
        } else if (!settings.hasLoc && !resolveLocation()) {
            wxErr = 6;                                    
        } else {
            wxUseF = settings.useF;

            char url[256];
            snprintf(url, sizeof url,
                     "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                     "&current=temperature_2m,weather_code,is_day"
                     "&daily=sunrise,sunset&timezone=auto&forecast_days=1"
                     "&temperature_unit=%s",
                     settings.lat, settings.lon, settings.useF ? "fahrenheit" : "celsius");

            WiFiClient client;
            HTTPClient http;
            http.setConnectTimeout(8000);
            http.setTimeout(8000);

            if (!http.begin(client, url)) {
                wxErr = 2;
            } else {
                int code = http.GET();
                if (code != HTTP_CODE_OK) {
                    wxErr = 3;
                } else {
                    JsonDocument doc;
                    if (deserializeJson(doc, http.getString())) {
                        wxErr = 4;
                    } else {
                        JsonObject cur = doc["current"];
                        if (cur.isNull()) {
                            wxErr = 5;
                        } else {
                            wxTempF = (int)lroundf(cur["temperature_2m"] | 0.0f);
                            wxCode  = cur["weather_code"] | -1;
                            wxIsDay = ((int)(cur["is_day"] | 1)) != 0;
                            wxValid = true;
                            wxErr   = 0;
                            ok      = true;

                            JsonObject daily = doc["daily"];
                            if (!daily.isNull()) {
                                wxSunrise = isoToMinutes(
                                    daily["sunrise"][0] | (const char *)nullptr);
                                wxSunset  = isoToMinutes(
                                    daily["sunset"][0]  | (const char *)nullptr);
                            }

                            int         off = doc["utc_offset_seconds"] | 0;
                            const char *tzn = doc["timezone"] | "";
                            bool changed = !settings.hasOffset || off != settings.utcOffset
                                        || (tzn[0] && settings.tzName != tzn);
                            if (changed) {
                                settings.utcOffset = off;
                                settings.hasOffset = true;
                                strncpy(tzStaged, tzn, sizeof tzStaged - 1);
                                tzStaged[sizeof tzStaged - 1] = 0;
                                tzPending = true;
                            }
                        }
                    }
                }
                http.end();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(ok ? WX_PERIOD_MS : WX_RETRY_MS));
    }
}

static void parkOnboardLed()
{
#if CONFIG_IDF_TARGET_ESP32C6
    // nothing to park
#elif CONFIG_IDF_TARGET_ESP32C3
    pinMode(8, OUTPUT);
    digitalWrite(8, HIGH);
#else
    neopixelWrite(48, 0, 0, 0);
    pinMode(48, OUTPUT);
    digitalWrite(48, LOW);
#endif
}

static const char *bannerL1, *bannerL2;
static uint16_t    bannerCol;
static void paintBanner(GfxDirect &g)
{
    uint16_t bg = activeFace->background();
    g.fillScreen(bg);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(bannerCol, bg);
#ifdef AMOLED_C6
    g.drawString(bannerL1, 120, 108, 9);
#else
    g.drawString(bannerL1, 120, 108, 4);
#endif
    if (bannerL2) {
        g.setTextColor(0x7BEF, bg);
        g.drawString(bannerL2, 120, 138, 2);
    }
}
static void banner(const char *line1, const char *line2, uint16_t color)
{
    bannerL1 = line1; bannerL2 = line2; bannerCol = color;
    screenPaint(paintBanner);
}

static const char *setupStatus = "";
static void paintSetup(GfxDirect &g)
{
#ifdef AMOLED_C6
    const uint8_t HEAD = 9;
#else
    const uint8_t HEAD = 4;
#endif
    g.fillScreen(0x0000);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(0xFD20, 0x0000); g.drawString("WiFi setup",       120,  50, HEAD);
    g.setTextColor(0x9CD3, 0x0000); g.drawString("join the network", 120,  82, 2);
    g.setTextColor(0xFFFF, 0x0000); g.drawString(portal::AP_SSID,    120, 106, HEAD);
    g.setTextColor(0x9CD3, 0x0000); g.drawString("then open",        120, 134, 2);
    g.setTextColor(0xFFFF, 0x0000); g.drawString("192.168.4.1",      120, 158, HEAD);
    g.setTextColor(0x7BEF, 0x0000); g.drawString(setupStatus,        120, 196, 2);
}
static void drawSetupScreen(const char *status)
{
    setupStatus = status;
    screenPaint(paintSetup);
}

static const char *portalStatusText(portal::State s)
{
    switch (s) {
        case portal::CONNECTING: return "connecting...";
        case portal::OK:         return "connected";
        case portal::FAIL:       return "could not join, try again";
        default:                 return "waiting for a phone";
    }
}

static void runPortalUntilConnected()
{
    portal::start();
    portal::State shown = portal::FAIL;   
    for (;;) {
        portal::handle();
        portal::State s = portal::state();
        if (s != shown) { shown = s; drawSetupScreen(portalStatusText(s)); }
        if (s == portal::OK && portal::sinceState() > 4000) { portal::stop(); return; }
        delay(10);
    }
}

static bool bootHeld()
{
    pinMode(BOOT_BTN, INPUT_PULLUP);
    delay(20);
    if (digitalRead(BOOT_BTN) != LOW) return false;
    banner("Hold BOOT", "3 s to reset the WiFi setup", 0xFD20);
    uint32_t t0 = millis();
    while (digitalRead(BOOT_BTN) == LOW) {
        if (millis() - t0 > 3000) return true;
        delay(20);
    }
    return false;
}

static void seedDefaults()
{
    if (!settings.hasWifi() && strlen(WIFI_SSID)) {
        settings.ssid = WIFI_SSID;
        settings.pass = WIFI_PASS;
    }
    if (!settings.hasLoc && !settings.locQuery.length()) {
#if defined(WX_LAT) && defined(WX_LON)
        settings.lat    = atof(WX_LAT);
        settings.lon    = atof(WX_LON);
        settings.hasLoc = true;
#endif
        if (strlen(WX_LOCATION)) settings.locQuery = WX_LOCATION;
        settings.useF = strcmp(WX_UNITS, "celsius") != 0;
    }
}

static void showFace(int idx)
{
    faceIdx    = idx;
    activeFace = ROTATION[faceIdx];
    faceSince  = millis();
    lastActivityTime = millis();  
    lastSec    = -1;
    screenInvalidate();
    screenClear(activeFace->background());
}

#ifdef AMOLED_C6
static const uint8_t BRIGHT_STOPS[] = { 12, 22, 41, 75, 138, 255 };
static const int     BRIGHT_N       = (int)(sizeof BRIGHT_STOPS / sizeof BRIGHT_STOPS[0]);
static const int     BRIGHT_DAY_I   = 4;    
static const int     BRIGHT_NIGHT_I = 2;    

static void applyBrightness(int night)
{
    int i = (night ? BRIGHT_NIGHT_I : BRIGHT_DAY_I) + alarms.brightAdjust;
    if (i < 0)             i = 0;
    if (i > BRIGHT_N - 1)  i = BRIGHT_N - 1;
    screenSetBrightness(BRIGHT_STOPS[i]);
}

static const int BRIGHT_DRAG_PX = 300;
static bool  dragActive  = false;    
static bool  dragArmed   = false;    
static int   dragStartY  = 0, dragStartX = 0;
static int   dragStartAdj = 0;
static bool  dragSwallow = false;    

static void trackBrightness(const struct tm &t)
{
    const bool downNow = touch::pressed();

    if (downNow && !dragArmed && !dragActive) {          
        dragArmed  = true;
        dragStartX = touch::x();
        dragStartY = touch::y();
        dragStartAdj = alarms.brightAdjust;
        return;
    }
    if (!downNow) {                                      
        if (dragActive) {
            dragSwallow = true;                          
            alarmsSave();                                
        }
        dragArmed = dragActive = false;
        return;
    }
    if (!dragArmed && !dragActive) return;

    const int dx = touch::x() - dragStartX;
    const int dy = touch::y() - dragStartY;

    if (dragArmed) {
        if (abs(dx) < 24 && abs(dy) < 24) return;        
        dragArmed = false;
        if (abs(dy) * 2 <= abs(dx) * 3) return;          
        dragActive = true;
    }

    int night = isNightNow(t) ? 1 : 0;
    int base  = night ? BRIGHT_NIGHT_I : BRIGHT_DAY_I;
    int moved = -dy * (BRIGHT_N - 1) / BRIGHT_DRAG_PX;
    int adj   = dragStartAdj + moved;
    if (base + adj < 0)            adj = -base;
    if (base + adj > BRIGHT_N - 1) adj = BRIGHT_N - 1 - base;

    if (adj != alarms.brightAdjust) {
        alarms.brightAdjust = (int16_t)adj;
        applyBrightness(night);                          
    }
}
#endif

//==================================SETUP==================================
void setup()
{
    Serial.begin(115200);
    parkOnboardLed();

    faceIdx    = firstFaceIdx();
    activeFace = ROTATION[faceIdx];

    screenInit();
    screenClear(activeFace->background());
#ifdef AMOLED_C6
    touch::begin();
    audio::begin();
    alarmsLoad();
    audio::setVolume(alarms.volume);
    applyBrightness(0);        

    imu::begin();
    rtc::begin();
    if (time_t seed = rtc::readUTC()) {
        struct timeval tv = { .tv_sec = seed, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        timeValid = true;
    }
#endif

    for (int i = 0; i < ROTATION_N; i++) ROTATION[i]->init();
    faceSince = millis();

    settingsLoad();
    bool startOver = bootHeld();
    if (startOver) settingsClear();
    else           seedDefaults();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    bool up = false;
    if (!startOver && settings.hasWifi()) {
        banner("WiFi", settings.ssid.c_str(), 0xE71C);
        WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) {
            handleBootButtonClicks(); 
            if (portal::running()) break; 
            delay(50);
        }
        up = WiFi.status() == WL_CONNECTED;
    }
    if (!up) runPortalUntilConnected();     

    if (!settings.hasLoc && settings.locQuery.length()) {
        banner("Location", settings.locQuery.c_str(), 0xE71C);
        resolveLocation();
    }
    wxUseF = settings.useF;

    banner("NTP", "syncing...", 0xE71C);

    configTzTime(tzString(CFG_TZ), "pool.ntp.org", "time.google.com", "time.nist.gov");

    uint32_t t0 = millis();
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED
           && millis() - t0 < 20000) {
        handleBootButtonClicks(); 
        if (portal::running()) break;
        delay(50);
    }
    if (portal::running()) {
        runPortalUntilConnected();
    }
    timeValid = time(nullptr) > 1700000000;

#ifdef AMOLED_C6
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && timeValid)
        rtc::writeUTC(time(nullptr));
#endif

    xTaskCreatePinnedToCore(weatherTask, "weather", 10240, nullptr, 1, nullptr, 0);

    screenInvalidate();
    screenClear(activeFace->background());

#ifdef AMOLED_C6
    supervisor::begin();
#endif

    lastActivityTime = millis();
}

volatile uint32_t diagLoops = 0, diagFrames = 0, diagLastFrameMs = 0;

// --- 3. HÀM XỬ LÝ NÚT BOOT (MENU & CLICK) ---
void handleBootButtonClicks() {
    static uint32_t btnPressTime = 0;
    static bool isPressing = false;
    bool btnState = digitalRead(BOOT_BTN); 
    uint32_t now = millis();

    if (btnState == LOW && !isPressing) {
        if (now - lastDebounceTime > 50) {
            isPressing = true;
            btnPressTime = now;
            lastDebounceTime = now;
        }
    }

    if (btnState == HIGH && isPressing) {
        uint32_t holdDuration = now - btnPressTime;
        isPressing = false;

        if (holdDuration < 1000) { 
            if (currentAppState == STATE_CLOCK) {
                currentAppState = STATE_MENU;
                selectedMenuItem = 0;
                drawMenuScreen();
            } else {
                selectedMenuItem = (selectedMenuItem + 1) % TOTAL_MENU_ITEMS;
                drawMenuScreen();
            }
        } else { 
            if (currentAppState == STATE_MENU) {
                switch (selectedMenuItem) {
                    case 0: 
                        currentAppState = STATE_CLOCK;
                        screenInvalidate();
                        break;
                    case 1: 
                        showFace(nextFaceIdx(faceIdx));
                        currentAppState = STATE_CLOCK;
                        break;
                    case 2: 
                        if (!portal::running()) {
                            portal::start();
                        } else {
                            portal::stop();
                        }
                        currentAppState = STATE_CLOCK;
                        screenInvalidate();
                        break;
                    case 3: 
                        playAnimeGif();
                        currentAppState = STATE_MENU;
                        drawMenuScreen();
                        break;
                    case 4: 
                        screenPaint([](GfxDirect &g) {
                            g.fillScreen(0x0000);
                        }); 
                        esp_light_sleep_start();
                        currentAppState = STATE_CLOCK;
                        break;
                    default:
                        currentAppState = STATE_CLOCK;
                        screenInvalidate();
                        break;
                }
            }
        }
    }
}

// --- 1. HÀM VẼ GIAO DIỆN MENU ---
void drawMenuScreen() {
    screenPaint([](GfxDirect &g) {
        g.fillScreen(0x0000); 
        g.setTextDatum(MC_DATUM);
        
        g.setTextColor(0xFD20, 0x0000); 
        g.drawString("--- MENU ---", 120, 35, 4);

        for (int i = 0; i < TOTAL_MENU_ITEMS; i++) {
            int yPos = 80 + (i * 35);
            if (i == selectedMenuItem) {
                g.fillRect(20, yPos - 12, 200, 28, 0xFD20);
                g.setTextColor(0x0000, 0xFD20);
                g.drawString(menuLabels[i], 120, yPos, 2);
            } else {
                g.setTextColor(0x9CD3, 0x0000);
                g.drawString(menuLabels[i], 120, yPos, 2);
            }
        }
        
        g.setTextColor(0x7BEF, 0x0000);
        g.drawString("Nhan: Chon | M.Giu: Chon", 120, 50, 2);
    });
}

// --- 2. HÀM CHẠY ANIME GIF ---
int current_anime_frame = 0;
void playAnimeGif() {
    for (int repeat = 0; repeat < 1; repeat++) {
        for (int frame = 0; frame < TOTAL_FRAMES; frame++) {
            if (digitalRead(BOOT_BTN) == LOW) {
                delay(200);
                lastActivityTime = millis();
                screenInvalidate();
                return; 
            }

            current_anime_frame = frame; 

            screenPaint([](GfxDirect &g) {
            // Đặt tâm ảnh vào giữa màn hình (120, 120)
                int startX = (240 - ANIME_WIDTH) / 2;
                int startY = (240 - ANIME_HEIGHT) / 2;
                g.pushImage(startX, startY, ANIME_WIDTH, ANIME_HEIGHT, (uint16_t*)anime_frames[current_anime_frame]);
            });
            
            for (int d = 0; d < 10; d++) {
                if (digitalRead(BOOT_BTN) == LOW) {
                    delay(200);
                    lastActivityTime = millis();
                    screenInvalidate();
                    return;
                }
                delay(10); 
            }
        }
    }
}
    
// --- 4. HÀM CHUYỂN VÀO CHẾ ĐỘ NGỦ (LIGHT SLEEP) ---
void goToSleep() {
    playAnimeGif();
    delay(500); 
            
    screenPaint([](GfxDirect &g) {
        g.fillScreen(0x0000); 
    });

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);

    pinMode(BOOT_BTN, INPUT_PULLUP);
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable((gpio_num_t)BOOT_BTN, GPIO_INTR_LOW_LEVEL);

    Serial.flush(); 
    esp_light_sleep_start();

    lastActivityTime = millis();
    screenInvalidate();
    delay(200);
}

//==================================DRAW==================================
void loop() {
    handleBootButtonClicks();

    if (currentAppState == STATE_MENU) {
        delay(20);
        return; 
    }

    diagLoops++;
    SUP(P_TZ);

    if (tzPending) {
        tzPending = false;
        if (tzStaged[0]) settings.tzName = tzStaged;
        settingsSave();
        applyTimezone(CFG_TZ);
    }

    SUP(P_TIME);
    struct timeval tv;
    gettimeofday(&tv, nullptr);

    time_t   now = tv.tv_sec;
    struct tm t;
    localtime_r(&now, &t);

    SUP(P_PORTAL);
    portal::handle();

#ifdef AMOLED_C6
    // ...
#endif

    if (settings.rotateMs && millis() - faceSince >= settings.rotateMs) showFace(nextFaceIdx(faceIdx));

    const bool smooth = activeFace->smooth();
    const int  hz     = screenSweepHz();
    static uint32_t lastFrameMs = 0;

    bool due = smooth ? (hz == 0 || millis() - lastFrameMs >= (uint32_t)(1000 / hz))
                      : (t.tm_sec != lastSec || millis() - lastFrameMs >= 1000);

    if (due) {
        lastSec     = t.tm_sec;
        lastFrameMs = millis();
        diagFrames++;
        diagLastFrameMs = lastFrameMs;
        float sub = smooth ? tv.tv_usec / 1000000.0f : 0.0f;
        SUP(P_RENDER);
        screenRenderFace(activeFace, t, sub, portal::running());
        SUP_DREW();
    }

    SUP(P_NET);
    if (millis() - lastSyncCheck > 1000) {
        lastSyncCheck = millis();
        bool up = WiFi.status() == WL_CONNECTED;
        if (!timeValid && time(nullptr) > 1700000000) timeValid = true;

        statusCol = (timeValid && up) ? 0x2E68 : 0xFD20;

        static int lastNight = -1;
        int night = isNightNow(t) ? 1 : 0;
        if (night != lastNight) {
            lastNight = night;
#ifdef AMOLED_C6
            applyBrightness(night);
#else
            screenSetBrightness(night ? 110 : 255);
#endif
        }

        static uint32_t downSince = 0;
        static bool manualClosed = false; 
        
        if (up) {
            downSince = 0;
            manualClosed = false; 
        } else if (!downSince) {
            downSince = millis();
        }

        if (!up && downSince && millis() - downSince > PORTAL_AFTER_DOWN_MS
            && !portal::running() && !manualClosed) {
            portal::start();
        }

        if (portal::running() && up && portal::state() != portal::CONNECTING
            && portal::sinceState() > 4000) {
            portal::stop();
        }

        static uint32_t lastRetry = 0;
        if (!up && !portal::running() && millis() - lastRetry > 10000) {
            lastRetry = millis();
            WiFi.reconnect();
        }
    }

    if (!portal::running() && (millis() - lastActivityTime >= settings.sleepTimeout)) {
        goToSleep();
    }

    SUP(P_IDLE);
    SUP_ALIVE();

    delay(5);
}