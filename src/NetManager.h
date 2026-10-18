#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include "Config.h"

// Manages the WiFi connection:
//  - STA with DHCP or static IP
//  - no WiFi configured / connection fails -> access point with captive portal
//  - in AP mode the bridge keeps trying to reach the configured WiFi (if any)
class NetManager
{
public:
    void begin(const AppConfig &cfg, bool forceAp);
    void loop();
    void forceAp();                // start access point at runtime (button)

    bool isConnected() const;      // STA connected
    bool isApActive() const { return apActive; }
    String apSsid() const { return apName; }
    IPAddress localIp() const;
    int32_t rssi() const;

private:
    void startSta();
    void startAp(bool keepSta);
    void stopAp();

    const AppConfig *cfg = nullptr;
    DNSServer dns;
    bool apActive = false;
    bool apForced = false;
    String apName;
    bool everConnected = false;
    unsigned long lostSince = 0;     // time the connection was lost (0 = connected)
    unsigned long lastStatusLog = 0;
};
