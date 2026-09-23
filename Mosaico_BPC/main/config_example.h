#pragma once
// Copy this file to config.h for local overrides. config.h is ignored by Git.
// Empty credentials allow first-time setup entirely from Setting > WLAN.
#define BPC_WIFI_SSID ""
#define BPC_WIFI_PASSWORD ""
#define BPC_TX_GPIO 38
#define BPC_TX_DRIVE 2
#define BPC_CARRIER_HZ 68500
// BPC encodes UTC+8. This is a protocol requirement, not a display preference.
#define BPC_TIMEZONE_SECONDS (8 * 3600)
#define BPC_NTP_SERVER_1 "ntp.aliyun.com"
#define BPC_NTP_SERVER_2 "ntp.tencent.com"
#define BPC_NTP_SERVER_3 "pool.ntp.org"
#define BPC_NTP_MAX_AGE_SECONDS (6 * 3600)
