#include "Config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "SecretStore.h"

static const char *CONFIG_FILE = "/config.json";

bool ConfigStore::begin()
{
#if defined(ESP32)
    if (LittleFS.begin(true))
        return true;
#else
    if (LittleFS.begin())
        return true;
    // file system corrupt/empty -> format
    Serial.println(F("[CFG] LittleFS mount failed, formatting..."));
    if (LittleFS.format() && LittleFS.begin())
        return true;
#endif
    Serial.println(F("[CFG] LittleFS not available"));
    return false;
}

bool ConfigStore::load(AppConfig &cfg)
{
    bool secretsValid = SecretStore::load(cfg.password, cfg.adminPassword);

    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f)
    {
        Serial.println(F("[CFG] no config.json found"));
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err)
    {
        Serial.printf("[CFG] config.json invalid: %s\n", err.c_str());
        return false;
    }

    cfg.ssid = doc["ssid"] | "";
    cfg.hostname = doc["hostname"] | cfg.hostname;
    cfg.dhcp = doc["dhcp"] | true;
    cfg.ip = doc["ip"] | "";
    cfg.gateway = doc["gateway"] | "";
    cfg.subnet = doc["subnet"] | cfg.subnet;
    cfg.dns = doc["dns"] | "";
    cfg.marstekIp = doc["marstekIp"] | "";
    cfg.marstekPort = doc["marstekPort"] | 30000;
    cfg.pollInterval = doc["pollInterval"] | 30;
    cfg.homeeId = doc["homeeId"] | cfg.homeeId;
    cfg.nodeName = doc["nodeName"] | cfg.nodeName;

    if (cfg.pollInterval < 10)
        cfg.pollInterval = 10;

    // Migration from <= 0.07: the WiFi password was stored in config.json.
    // Move it to the EEPROM and remove it from the file.
    if (!doc["password"].isNull())
    {
        if (!secretsValid)
            cfg.password = doc["password"] | "";
        Serial.println(F("[CFG] moving WiFi password from config.json to EEPROM"));
        save(cfg);
        return true;
    }

    // Add parameters introduced by a firmware update with their defaults,
    // existing values (incl. WiFi) remain unchanged
    static const char *const KEYS[] = {"ssid", "hostname", "dhcp", "ip", "gateway", "subnet", "dns",
                                       "marstekIp", "marstekPort", "pollInterval", "homeeId", "nodeName"};
    for (const char *k : KEYS)
    {
        if (doc[k].isNull())
        {
            Serial.printf("[CFG] parameter '%s' missing, adding default\n", k);
            save(cfg);
            break;
        }
    }
    return true;
}

bool ConfigStore::save(const AppConfig &cfg)
{
    JsonDocument doc;
    doc["ssid"] = cfg.ssid;
    doc["hostname"] = cfg.hostname;
    doc["dhcp"] = cfg.dhcp;
    doc["ip"] = cfg.ip;
    doc["gateway"] = cfg.gateway;
    doc["subnet"] = cfg.subnet;
    doc["dns"] = cfg.dns;
    doc["marstekIp"] = cfg.marstekIp;
    doc["marstekPort"] = cfg.marstekPort;
    doc["pollInterval"] = cfg.pollInterval;
    doc["homeeId"] = cfg.homeeId;
    doc["nodeName"] = cfg.nodeName;

    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f)
    {
        Serial.println(F("[CFG] config.json cannot be written"));
        return false;
    }
    size_t n = serializeJson(doc, f);
    f.close();
    Serial.printf("[CFG] config.json saved (%u bytes)\n", (unsigned)n);
    bool secretsOk = SecretStore::save(cfg.password, cfg.adminPassword);
    return n > 0 && secretsOk;
}

void ConfigStore::erase()
{
    LittleFS.remove(CONFIG_FILE);
    SecretStore::erase();
}
