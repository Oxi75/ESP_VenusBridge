#include "NetManager.h"

#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

static const unsigned long STA_CONNECT_TIMEOUT = 30000;   // first connection attempt
static const unsigned long STA_LOST_TIMEOUT = 300000;     // connection lost -> AP after 5 min
static const char *AP_PASSWORD = "venusbridge";          // at least 8 characters
static const IPAddress AP_IP(192, 168, 4, 1);

void NetManager::begin(const AppConfig &config, bool forceAp)
{
    cfg = &config;
    apForced = forceAp;

    char suffix[8];
#if defined(ESP32)
    snprintf(suffix, sizeof(suffix), "%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
#else
    snprintf(suffix, sizeof(suffix), "%04X", (uint16_t)(ESP.getChipId() & 0xFFFF));
#endif
    apName = String("VenusBridge-") + suffix;

    WiFi.persistent(false);   // credentials only in config.json, not in the SDK flash area
    WiFi.setAutoReconnect(true);

    if (forceAp || !cfg->isWifiConfigured())
    {
        startAp(false);
        return;
    }
    startSta();
}

// Some boards (e.g. ESP32-C3 SuperMini) are unstable when transmitting at full power
static void applyTxPower()
{
#ifdef VB_WIFI_TX_POWER
    WiFi.setTxPower(VB_WIFI_TX_POWER);
#endif
}

void NetManager::startSta()
{
#if defined(ESP32)
    WiFi.setHostname(cfg->hostname.c_str());   // must be set before the STA interface starts
    WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
#else
    WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
    WiFi.hostname(cfg->hostname);
#endif
    applyTxPower();

    if (!cfg->dhcp)
    {
        IPAddress ip, gw, sn, dnsIp;
        if (ip.fromString(cfg->ip) && gw.fromString(cfg->gateway) && sn.fromString(cfg->subnet))
        {
            if (!dnsIp.fromString(cfg->dns))
                dnsIp = gw;
            WiFi.config(ip, gw, sn, dnsIp);
        }
        else
        {
            Serial.println(F("[NET] static IP invalid, using DHCP"));
        }
    }

    Serial.printf("[NET] connecting to '%s'...\n", cfg->ssid.c_str());
    WiFi.begin(cfg->ssid.c_str(), cfg->password.c_str());
    lostSince = millis();
}

void NetManager::forceAp()
{
    if (apForced && apActive)
        return;
    Serial.println(F("[NET] configuration mode requested"));
    apForced = true;   // AP stays active until the next restart
    startAp(cfg->isWifiConfigured());
}

void NetManager::startAp(bool keepSta)
{
    if (apActive)
        return;
    WiFi.mode(keepSta ? WIFI_AP_STA : WIFI_AP);
    applyTxPower();
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(apName.c_str(), AP_PASSWORD);
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", AP_IP);
    apActive = true;
    Serial.printf("[NET] access point '%s' active, IP %s, password '%s'\n",
                  apName.c_str(), AP_IP.toString().c_str(), AP_PASSWORD);
}

void NetManager::stopAp()
{
    if (!apActive)
        return;
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apActive = false;
    Serial.println(F("[NET] access point stopped"));
}

void NetManager::loop()
{
    if (apActive)
        dns.processNextRequest();

    if (apForced || !cfg->isWifiConfigured())
        return;

    unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED)
    {
        if (lostSince != 0)
        {
            Serial.printf("[NET] connected, IP %s, RSSI %d dBm\n",
                          WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
            lostSince = 0;
            everConnected = true;
        }
        // only stop the AP if nobody is currently using the portal
        if (apActive && WiFi.softAPgetStationNum() == 0)
            stopAp();
        return;
    }

    if (lostSince == 0)
    {
        Serial.println(F("[NET] WiFi connection lost"));
        lostSince = now;
    }

    unsigned long timeout = everConnected ? STA_LOST_TIMEOUT : STA_CONNECT_TIMEOUT;
    if (!apActive && now - lostSince > timeout)
    {
        Serial.println(F("[NET] WiFi not reachable -> starting access point"));
        startAp(true);
    }

    if (now - lastStatusLog > 10000)
    {
        lastStatusLog = now;
        Serial.printf("[NET] waiting for WiFi (status %d)\n", (int)WiFi.status());
    }
}

bool NetManager::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}

IPAddress NetManager::localIp() const
{
    return isConnected() ? WiFi.localIP() : WiFi.softAPIP();
}

int32_t NetManager::rssi() const
{
    return isConnected() ? WiFi.RSSI() : 0;
}
