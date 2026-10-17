// VenusBridge – Marstek Venus E (Mini) <-> homee
//
// Reads the battery via the local Marstek Open API (UDP) and provides the values
// as a device in homee via "homee in homee" (homee-api-esp32).

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "Config.h"
#include "HomeeBridge.h"
#include "MarstekClient.h"
#include "NetManager.h"
#include "WebUi.h"

#ifndef VB_VERSION
#define VB_VERSION "dev"
#endif

static AppConfig config;
static NetManager net;
static MarstekClient marstek;
static HomeeBridge homee;
static WebUi web;
static bool servicesStarted = false;

// ---- Status LED -------------------------------------------------------------
// on: everything OK | slow blinking: battery not reachable
// fast blinking: access point active | short flash: no WiFi
static void ledWrite(bool on)
{
#ifdef VB_LED_PIN
    digitalWrite(VB_LED_PIN, (on ? 1 : 0) ^ VB_LED_ACTIVE_LOW);
#endif
}

static void ledLoop()
{
    unsigned long t = millis();
    if (net.isApActive())
        ledWrite((t / 150) % 2);
    else if (!net.isConnected())
        ledWrite((t % 2000) < 80);
    else if (!marstek.state().online)
        ledWrite((t / 1000) % 2);
    else
        ledWrite(true);
}

// ---- Configuration button ----------------------------------------------------
// Hold >= 3 s during operation -> start access point. The button must have been
// released once after boot.
#ifdef VB_CONFIG_PIN
static const unsigned long CONFIG_HOLD_MS = 3000;

static void buttonLoop()
{
    static bool armed = false;
    static unsigned long pressedSince = 0;

    bool pressed = digitalRead(VB_CONFIG_PIN) == LOW;
    unsigned long now = millis();
    if (!pressed)
    {
        armed = true;
        pressedSince = 0;
        return;
    }
    if (!armed)
        return;
    if (pressedSince == 0)
        pressedSince = now | 1;   // 0 = not pressed
    else if (now - pressedSince >= CONFIG_HOLD_MS)
    {
        armed = false;   // trigger again only after release
        Serial.println(F("configuration button held -> access point"));
        net.forceAp();
    }
}
#endif

// ---- OTA from PlatformIO / Arduino IDE (espota) -----------------------------
// ArduinoOTA runs entirely in the loop() context; yield()/delay() are allowed there.
static void setupOta()
{
    ArduinoOTA.setHostname(config.hostname.c_str());
    ArduinoOTA.onStart([]() {
        Serial.printf("[OTA] start (%s)\n", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "file system");
    });
    ArduinoOTA.onEnd([]() { Serial.println(F("\n[OTA] done, restarting")); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t lastPct = 255;
        uint8_t pct = progress * 100 / total;
        if (pct / 10 != lastPct / 10)
            Serial.printf("[OTA] %u %%\n", pct);
        lastPct = pct;
        ledWrite((progress / 4096) % 2);
    });
    ArduinoOTA.onError([](ota_error_t error) { Serial.printf("[OTA] error %u\n", (unsigned)error); });
    ArduinoOTA.begin();
    Serial.printf("[OTA] ready as '%s'\n", config.hostname.c_str());
}

// ---- Setup / Loop -----------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\nVenusBridge %s\n", VB_VERSION);

#ifdef VB_LED_PIN
    pinMode(VB_LED_PIN, OUTPUT);
    ledWrite(false);
#endif

    bool forceAp = false;
#ifdef VB_CONFIG_PIN
    pinMode(VB_CONFIG_PIN, INPUT_PULLUP);
#if VB_CONFIG_PIN_AT_BOOT
    // Button pressed at boot -> force configuration mode
    // (not for strapping pins like GPIO9 on the ESP32-C3: that starts the bootloader)
    delay(10);
    forceAp = digitalRead(VB_CONFIG_PIN) == LOW;
    if (forceAp)
        Serial.println(F("configuration button pressed -> access point"));
#endif
#endif

    ConfigStore::begin();
    ConfigStore::load(config);

    net.begin(config, forceAp);
    web.begin(config, net, marstek, homee);
}

void loop()
{
    net.loop();
    web.loop();

    // Start Marstek polling and homee only once WiFi has been connected
    if (!servicesStarted && net.isConnected())
    {
        servicesStarted = true;
        marstek.begin(config.marstekIp, config.marstekPort, config.pollInterval);
        homee.begin(config.homeeId, config.nodeName, marstek);
        setupOta();
    }

    if (servicesStarted)
    {
        ArduinoOTA.handle();
        marstek.loop();
        homee.loop();
    }

#ifdef VB_CONFIG_PIN
    buttonLoop();
#endif
    ledLoop();
#if defined(ESP32)
    delay(1);   // single-core C3: let the idle task (watchdog) and other tasks run
#else
    yield();
#endif
}
