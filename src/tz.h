// ---------------------------------------------------------------------------
//  Timezone. The geocoder gives an IANA zone name; the C library wants a
//  POSIX rule string with the DST changeover dates in it. This maps the
//  common zones exactly and, for anything else, builds a fixed-offset rule
//  from the UTC offset the weather feed reports (refreshed every ten
//  minutes, so a DST change is picked up within that).
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>
#include <time.h>
#include "settings.h"

struct TzRule { const char *iana; const char *posix; };

static const TzRule TZ_TABLE[] = {
    // North America
    {"America/New_York",      "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Detroit",       "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Toronto",       "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Montreal",      "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Indiana/Indianapolis", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Kentucky/Louisville",  "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago",       "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Winnipeg",      "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City",   "CST6"},
    {"America/Denver",        "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Edmonton",      "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Boise",         "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix",       "MST7"},
    {"America/Los_Angeles",   "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Vancouver",     "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Tijuana",       "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Anchorage",     "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Pacific/Honolulu",      "HST10"},
    {"America/Halifax",       "AST4ADT,M3.2.0,M11.1.0"},
    {"America/St_Johns",      "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Puerto_Rico",   "AST4"},
    // South America
    {"America/Sao_Paulo",     "<-03>3"},
    {"America/Argentina/Buenos_Aires", "<-03>3"},
    {"America/Bogota",        "<-05>5"},
    {"America/Lima",          "<-05>5"},
    // Europe
    {"Europe/London",         "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin",         "IST-1GMT0,M10.5.0,M3.5.0/1"},
    {"Europe/Lisbon",         "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Paris",          "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome",           "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam",      "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm",      "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Oslo",           "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen",     "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Prague",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Budapest",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Bucharest",      "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Athens",         "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kyiv",           "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kiev",           "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Sofia",          "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul",       "<+03>-3"},
    {"Europe/Moscow",         "MSK-3"},
    // Africa, Middle East, Asia
    {"Africa/Cairo",          "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Africa/Johannesburg",   "SAST-2"},
    {"Africa/Lagos",          "WAT-1"},
    {"Africa/Nairobi",        "EAT-3"},
    {"Asia/Jerusalem",        "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Asia/Dubai",            "<+04>-4"},
    {"Asia/Karachi",          "PKT-5"},
    {"Asia/Kolkata",          "IST-5:30"},
    {"Asia/Dhaka",            "<+06>-6"},
    {"Asia/Bangkok",          "<+07>-7"},
    {"Asia/Jakarta",          "WIB-7"},
    {"Asia/Shanghai",         "CST-8"},
    {"Asia/Hong_Kong",        "HKT-8"},
    {"Asia/Singapore",        "<+08>-8"},
    {"Asia/Taipei",           "CST-8"},
    {"Asia/Manila",           "PST-8"},
    {"Asia/Tokyo",            "JST-9"},
    {"Asia/Seoul",            "KST-9"},
    {"Asia/Yakutsk",          "YAKT-9YAKST,M3.5.0,M10.5.0/3"},
    {"Asia/Vladivostok",      "VLAT-10VLAST,M3.5.0,M10.5.0/3"},
    {"Asia/Magadan",          "MAGT-11MAGST,M3.5.0,M10.5.0/3"},
    {"Asia/Srednekolymsk",    "SRET-11SRETS,M3.5.0,M10.5.0/3"},
    {"Asia/Kamchatka",        "PETT-12PETST,M3.5.0,M10.5.0/3"},
    {"Asia/Krasnoyarsk",      "KRAT-7KRASST,M3.5.0,M10.5.0/3"},
    {"Asia/Yekaterinburg",    "YEKT-5YEKST,M3.5.0,M10.5.0/3"},
    {"Asia/Omsk",             "OMST-6OMSST,M3.5.0,M10.5.0/3"},
    {"Asia/Novosibirsk",      "NOVT-7NOVST,M3.5.0,M10.5.0/3"},
    {"Asia/Irkutsk",          "IRKT-8IRKST,M3.5.0,M10.5.0/3"},
    {"Asia/Almaty",           "ALMT-6ALMST,M3.5.0,M10.5.0/3"},
    {"Asia/Tashkent",         "UZT-5UZST,M3.5.0,M10.5.0/3"},
    {"Asia/Bishkek",          "KGT-6KGTST,M3.5.0,M10.5.0/3"},
    {"Asia/Dushanbe",         "TJT-5TJST,M3.5.0,M10.5.0/3"},
    {"Asia/Kabul",            "AFT-4:30"},
    {"Asia/Yerevan",          "AMT-4AMST,M3.5   .0,M10.5.0/3"},
    {"Asia/Tbilisi",          "GET-4GETST,M3.5.0,M10.5.0/3"},
    {"Asia/Baku",             "AZT-4AZST,M3.5.0,M10.5.0/3"},
    {"Asia/Riyadh",           "<+03>-3"},
    {"Asia/Baghdad",          "AST-3"},
    {"Asia/Tehran",           "IRST-3:30IRDT,M3.22.0,M9.22.0/24"},
    {"Asia/Kuwait",           "<+03>-3"},
    {"Asia/Doha",             "<+03>-3"},
    {"Asia/Muscat",           "<+04>-4"},
    {"Asia/Viet",          "IST-5:30"},
    // Oceania
    {"Australia/Sydney",      "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne",   "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Hobart",      "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane",    "AEST-10"},
    {"Australia/Adelaide",    "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth",       "AWST-8"},
    {"Pacific/Auckland",      "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"UTC",                   "UTC0"},
};

inline const char *posixForZone(const String &iana)
{
    for (const TzRule &r : TZ_TABLE)
        if (iana == r.iana) return r.posix;
    return nullptr;
}

// The rule string to use, in order of preference: an explicit TZ_INFO from
// config.h, the table, a fixed offset from the weather feed, and finally UTC.
inline const char *tzString(const char *override)
{
    static char buf[32];
    if (override && override[0]) return override;
    if (const char *p = posixForZone(settings.tzName)) return p;
    if (settings.hasOffset) {
        // POSIX counts west as positive, so the sign flips
        int o = -settings.utcOffset;
        int h = abs(o) / 3600, m = (abs(o) % 3600) / 60;
        snprintf(buf, sizeof buf, "LOC%s%d:%02d", o < 0 ? "-" : "", h, m);
        return buf;
    }
    return "UTC0";
}

inline void applyTimezone(const char *override)
{
    setenv("TZ", tzString(override), 1);
    tzset();
}
