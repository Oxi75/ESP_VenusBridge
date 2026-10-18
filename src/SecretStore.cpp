#include "SecretStore.h"

#include <EEPROM.h>

// Layout: magic 'V','B' | version | wifiLen | wifi[64] | adminLen | admin[32] | checksum
static const size_t EEPROM_SIZE = 128;
static const uint8_t VERSION = 1;
static const size_t OFS_WIFI_LEN = 3;
static const size_t OFS_WIFI = OFS_WIFI_LEN + 1;
static const size_t OFS_ADMIN_LEN = OFS_WIFI + SecretStore::WIFI_PW_MAX;
static const size_t OFS_ADMIN = OFS_ADMIN_LEN + 1;
static const size_t OFS_CHECKSUM = OFS_ADMIN + SecretStore::ADMIN_PW_MAX;

static const char KEY[] = "VenusBridge#Secret*2026";

static bool begun = false;

static void beginEeprom()
{
    if (!begun)
    {
        EEPROM.begin(EEPROM_SIZE);
        begun = true;
    }
}

// XOR with the key and the position, so equal characters give different bytes
static uint8_t mask(size_t pos)
{
    return (uint8_t)KEY[pos % (sizeof(KEY) - 1)] ^ (uint8_t)(pos * 31 + 7);
}

static uint8_t checksum()
{
    uint8_t c = 0x5A;
    for (size_t i = 0; i < OFS_CHECKSUM; i++)
        c = (uint8_t)((c << 1) | (c >> 7)) ^ EEPROM.read(i);
    return c;
}

static void writeField(size_t lenOfs, size_t dataOfs, size_t maxLen, const String &value)
{
    size_t len = value.length() > maxLen ? maxLen : value.length();
    EEPROM.write(lenOfs, (uint8_t)len ^ mask(lenOfs));
    for (size_t i = 0; i < maxLen; i++)
    {
        uint8_t b = i < len ? (uint8_t)value[i] : 0;
        EEPROM.write(dataOfs + i, b ^ mask(dataOfs + i));
    }
}

static bool readField(size_t lenOfs, size_t dataOfs, size_t maxLen, String &value)
{
    size_t len = EEPROM.read(lenOfs) ^ mask(lenOfs);
    if (len > maxLen)
        return false;
    value = "";
    value.reserve(len);
    for (size_t i = 0; i < len; i++)
        value += (char)(EEPROM.read(dataOfs + i) ^ mask(dataOfs + i));
    return true;
}

bool SecretStore::load(String &wifiPw, String &adminPw)
{
    beginEeprom();
    if (EEPROM.read(0) != 'V' || EEPROM.read(1) != 'B' || EEPROM.read(2) != VERSION ||
        EEPROM.read(OFS_CHECKSUM) != checksum())
    {
        Serial.println(F("[CFG] no valid secrets in EEPROM"));
        return false;
    }
    String w, a;
    if (!readField(OFS_WIFI_LEN, OFS_WIFI, WIFI_PW_MAX, w) || !readField(OFS_ADMIN_LEN, OFS_ADMIN, ADMIN_PW_MAX, a))
    {
        Serial.println(F("[CFG] secrets in EEPROM invalid"));
        return false;
    }
    wifiPw = w;
    adminPw = a;
    return true;
}

bool SecretStore::save(const String &wifiPw, const String &adminPw)
{
    beginEeprom();
    EEPROM.write(0, 'V');
    EEPROM.write(1, 'B');
    EEPROM.write(2, VERSION);
    writeField(OFS_WIFI_LEN, OFS_WIFI, WIFI_PW_MAX, wifiPw);
    writeField(OFS_ADMIN_LEN, OFS_ADMIN, ADMIN_PW_MAX, adminPw);
    EEPROM.write(OFS_CHECKSUM, checksum());
    bool ok = EEPROM.commit();
    Serial.println(ok ? F("[CFG] secrets saved to EEPROM") : F("[CFG] secrets cannot be written to EEPROM"));
    return ok;
}

void SecretStore::erase()
{
    beginEeprom();
    for (size_t i = 0; i < EEPROM_SIZE; i++)
        EEPROM.write(i, 0xFF);
    EEPROM.commit();
}
