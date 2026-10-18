#pragma once

#include <Arduino.h>

// Passwords (WiFi, admin), stored XOR-obfuscated in the (emulated) EEPROM.
// Obfuscation only, not real encryption: it keeps the passwords out of
// config.json and hides them from a plain look at a flash dump.
class SecretStore
{
public:
    static const size_t WIFI_PW_MAX = 64;
    static const size_t ADMIN_PW_MAX = 32;

    static bool load(String &wifiPw, String &adminPw);   // false if no valid record
    static bool save(const String &wifiPw, const String &adminPw);
    static void erase();
};
