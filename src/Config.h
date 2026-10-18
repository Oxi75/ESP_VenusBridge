#pragma once

#include <Arduino.h>

// Persistent settings, stored as /config.json on LittleFS.
// Passwords are not part of config.json, they are kept in the EEPROM (SecretStore).
struct AppConfig
{
    // WiFi
    String ssid;
    String password;
    String hostname = "venusbridge";
    bool dhcp = true;
    String ip;      // only used when dhcp == false
    String gateway;
    String subnet = "255.255.255.0";
    String dns;

    // Marstek Open API
    String marstekIp;
    uint16_t marstekPort = 30000;
    uint16_t pollInterval = 30;   // seconds between two polling cycles

    // homee
    String homeeId = "venusbridge";   // virtual homee name (homee ID when pairing, UDP discovery)
    String nodeName = "Venus E";      // homee device name (name of the device in homee)

    // Security
    String adminPassword;   // empty = no protection of saving / firmware update

    bool isWifiConfigured() const { return ssid.length() > 0; }
    bool isAdminPasswordSet() const { return adminPassword.length() > 0; }
};

class ConfigStore
{
public:
    static bool begin();                     // mount LittleFS
    static bool load(AppConfig &cfg);        // false if the file is missing/invalid
    static bool save(const AppConfig &cfg);
    static void erase();
};
