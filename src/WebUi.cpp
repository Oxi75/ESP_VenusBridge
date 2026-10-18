#include "WebUi.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#if defined(ESP32)
#include <Update.h>
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#include <Updater.h>
#endif

#include "HomeeBridge.h"
#include "MarstekClient.h"
#include "NetManager.h"
#include "SecretStore.h"
#include "StateLock.h"
#include "WebPages.h"

#ifndef VB_VERSION
#define VB_VERSION "dev"
#endif

static const size_t ADMIN_PW_MIN = 5;
static const unsigned long AUTH_BLOCK_MS = 2000;

// Decodes %XX sequences (the browser sends the password with encodeURIComponent)
static String urlDecode(const String &in)
{
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++)
    {
        char c = in[i];
        if (c == '%' && i + 2 < in.length() &&isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2]))
        {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        }
        else
            out += c;
    }
    return out;
}

// Comparison without early exit (timing does not reveal the matching prefix)
static bool secureEquals(const String &a, const String &b)
{
    if (a.length() != b.length())
        return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.length(); i++)
        diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return diff == 0;
}

static void sendPage(AsyncWebServerRequest *req, const char *page, const char *type)
{
    AsyncWebServerResponse *res = req->beginResponse(200, type, (const uint8_t *)page, strlen_P(page));
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
}

void WebUi::begin(AppConfig &config, NetManager &netMgr, MarstekClient &client, HomeeBridge &hb)
{
    cfg = &config;
    net = &netMgr;
    mc = &client;
    homee = &hb;
    server = new AsyncWebServer(80);

    server->on("/", HTTP_GET, [this](AsyncWebServerRequest *req) {
        // In pure AP mode go directly to the settings
        if (net->isApActive() && !net->isConnected())
            req->redirect("/config");
        else
            sendPage(req, PAGE_INDEX, "text/html");
    });
    server->on("/config", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, PAGE_CONFIG, "text/html"); });
    server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest *req) { sendPage(req, PAGE_CSS, "text/css"); });
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *req) { handleStatus(req); });
    server->on("/api/config", HTTP_GET, [this](AsyncWebServerRequest *req) { handleGetConfig(req); });
    server->on("/api/config", HTTP_POST, [this](AsyncWebServerRequest *req) { handleSetConfig(req); });
    server->on("/api/restart", HTTP_POST, [this](AsyncWebServerRequest *req) {
        req->send(200, "text/plain", "OK");
        restartAt = millis() + 1000;
    });

    // Firmware update via file upload (password protected)
    server->on(
        "/update", HTTP_POST,
        [this](AsyncWebServerRequest *req) {
            if (!checkAuth(req))
                return;
            bool ok = !Update.hasError();
            req->send(ok ? 200 : 500, "text/plain", ok ? "OK, restarting..." : "Update failed");
            if (ok)
                restartAt = millis() + 1000;
        },
        [this](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0)
            {
                uploadAuthorized = authState(req) == Auth::Ok;
                if (!uploadAuthorized)
                {
                    Serial.println(F("[WEB] firmware update rejected: not authorized"));
                    return;
                }
                Serial.printf("[WEB] firmware update: %s\n", filename.c_str());
#if defined(ESP8266)
                Update.runAsync(true);
                uint32_t maxSize = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
#else
                uint32_t maxSize = UPDATE_SIZE_UNKNOWN;
#endif
                if (!Update.begin(maxSize))
                    Update.printError(Serial);
            }
            if (!uploadAuthorized)
                return;
            if (!Update.hasError() && Update.write(data, len) != len)
                Update.printError(Serial);
            if (final)
            {
                if (Update.end(true))
                    Serial.printf("[WEB] update OK, %u bytes\n", (unsigned)(index + len));
                else
                    Update.printError(Serial);
            }
        });

    // Captive portal: redirect all unknown requests in AP mode to the settings
    server->onNotFound([this](AsyncWebServerRequest *req) {
        if (net->isApActive())
            req->redirect(String("http://") + WiFi.softAPIP().toString() + "/config");
        else
            req->send(404, "text/plain", "Not found");
    });

    server->begin();
    Serial.println(F("[WEB] web interface started on port 80"));
}

void WebUi::loop()
{
    if (restartAt != 0 && (long)(millis() - restartAt) >= 0)
    {
        Serial.println(F("[WEB] restarting"));
        delay(100);
        ESP.restart();
    }
}

// Request arrived via the access point interface (configuration mode)
bool WebUi::isViaAp(AsyncWebServerRequest *req) const
{
    return net->isApActive() && req->client() && req->client()->localIP() == WiFi.softAPIP();
}

bool WebUi::isAuthRequired(AsyncWebServerRequest *req) const
{
    return cfg->isAdminPasswordSet() && !isViaAp(req);
}

WebUi::Auth WebUi::authState(AsyncWebServerRequest *req) const
{
    if (!isAuthRequired(req))
        return Auth::Ok;
    if (authBlockedUntil != 0 && (long)(millis() - authBlockedUntil) < 0)
        return Auth::Blocked;
    const AsyncWebHeader *h = req->getHeader("X-Auth");
    if (h && secureEquals(urlDecode(h->value()), cfg->adminPassword))
        return Auth::Ok;
    return Auth::Denied;
}

bool WebUi::checkAuth(AsyncWebServerRequest *req)
{
    switch (authState(req))
    {
    case Auth::Ok:
        return true;
    case Auth::Blocked:
        req->send(429, "text/plain", "Too many attempts, please wait a moment");
        return false;
    default:
        Serial.println(F("[WEB] wrong admin password"));
        authBlockedUntil = (millis() + AUTH_BLOCK_MS) | 1;   // 0 = not blocked
        req->send(401, "text/plain", "Wrong admin password");
        return false;
    }
}

void WebUi::handleStatus(AsyncWebServerRequest *req)
{
    const VenusState &s = mc->state();
    JsonDocument doc;
    doc["version"] = VB_VERSION;
    doc["ip"] = net->localIp().toString();
    doc["ssid"] = cfg->ssid;
    doc["rssi"] = net->rssi();
    doc["ap"] = net->isApActive() ? net->apSsid() : "";
    doc["homeeClients"] = homee->clientCount();
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
#if defined(ESP8266)
    doc["maxBlock"] = ESP.getMaxFreeBlockSize();
#else
    doc["maxBlock"] = ESP.getMaxAllocHeap();
#endif

    StateLock lock;   // state is written in loop() (ESP32: different task)
    doc["raw"] = mc->lastRaw();

    JsonObject v = doc["venus"].to<JsonObject>();
    v["configured"] = mc->isConfigured();
    v["online"] = s.online;
    v["device"] = s.device;
    v["fw"] = s.fwVersion;
    v["ok"] = s.okCount;
    v["err"] = s.errCount;
    v["esValid"] = s.esValid;
    v["soc"] = s.soc;
    v["batCapWh"] = s.batCapWh;
    v["batPower"] = s.batPower;
    v["batPowerCalc"] = s.batPowerCalculated;
    v["pvPower"] = s.pvPower;
    v["ongridPower"] = s.ongridPower;
    v["offgridPower"] = s.offgridPower;
    v["gridOutWh"] = s.gridOutWh;
    v["gridInWh"] = s.gridInWh;
    v["modeValid"] = s.modeValid;
    v["mode"] = venusModeName(s.mode);
    v["emValid"] = s.emValid;
    v["emTotalPower"] = s.emTotalPower;
    v["emA"] = s.emA;
    v["emB"] = s.emB;
    v["emC"] = s.emC;
    v["batValid"] = s.batValid;
    v["batTemp"] = s.batTemp;
    v["ratedCapWh"] = s.ratedCapWh;

    AsyncResponseStream *res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
}

void WebUi::handleGetConfig(AsyncWebServerRequest *req)
{
    JsonDocument doc;
    doc["ssid"] = cfg->ssid;
    doc["hostname"] = cfg->hostname;
    doc["dhcp"] = cfg->dhcp;
    doc["ip"] = cfg->ip;
    doc["gateway"] = cfg->gateway;
    doc["subnet"] = cfg->subnet;
    doc["dns"] = cfg->dns;
    doc["marstekIp"] = cfg->marstekIp;
    doc["marstekPort"] = cfg->marstekPort;
    doc["pollInterval"] = cfg->pollInterval;
    doc["homeeId"] = cfg->homeeId;
    doc["nodeName"] = cfg->nodeName;
    doc["adminPwSet"] = cfg->isAdminPasswordSet();
    doc["authRequired"] = isAuthRequired(req);
    // the passwords are intentionally not returned

    AsyncResponseStream *res = req->beginResponseStream("application/json");
    serializeJson(doc, *res);
    req->send(res);
}

void WebUi::handleSetConfig(AsyncWebServerRequest *req)
{
    if (!checkAuth(req))
        return;

    auto arg = [req](const char *name) -> String {
        const AsyncWebParameter *p = req->getParam(name, true);
        return p ? p->value() : String();
    };

    AppConfig c = *cfg;
    c.ssid = arg("ssid");
    String pw = arg("password");
    if (pw.length() > 0)
        c.password = pw;
    c.hostname = arg("hostname");
    c.dhcp = arg("dhcp") != "0";
    c.ip = arg("ip");
    c.gateway = arg("gateway");
    c.subnet = arg("subnet");
    c.dns = arg("dns");
    c.marstekIp = arg("marstekIp");
    c.marstekPort = (uint16_t)arg("marstekPort").toInt();
    c.pollInterval = (uint16_t)arg("pollInterval").toInt();
    c.homeeId = arg("homeeId");
    c.nodeName = arg("nodeName");
    String adminPw = arg("adminPw");
    if (arg("adminPwClear") == "1")
        c.adminPassword = "";
    else if (adminPw.length() > 0)
        c.adminPassword = adminPw;

    // Validation
    IPAddress tmp;
    if (c.ssid.length() == 0)
        return req->send(400, "text/plain", "SSID missing");
    if (c.password.length() > SecretStore::WIFI_PW_MAX)
        return req->send(400, "text/plain", "WiFi password too long");
    if (c.adminPassword.length() > 0 &&
        (c.adminPassword.length() < ADMIN_PW_MIN || c.adminPassword.length() > SecretStore::ADMIN_PW_MAX))
        return req->send(400, "text/plain", "Admin password must have 5 to 32 characters");
    if (!c.dhcp && (!tmp.fromString(c.ip) || !tmp.fromString(c.gateway) || !tmp.fromString(c.subnet)))
        return req->send(400, "text/plain", "Static IP, gateway or subnet mask invalid");
    if (c.marstekIp.length() > 0 && !tmp.fromString(c.marstekIp))
        return req->send(400, "text/plain", "Battery IP address invalid");
    if (c.marstekPort == 0)
        c.marstekPort = 30000;
    if (c.pollInterval < 10)
        c.pollInterval = 10;
    if (c.hostname.length() == 0)
        c.hostname = "venusbridge";
    if (c.homeeId.length() == 0)
        c.homeeId = "venusbridge";
    if (c.nodeName.length() == 0)
        c.nodeName = "Venus E";

    if (!ConfigStore::save(c))
        return req->send(500, "text/plain", "Saving failed");
    // Intentionally do not modify *cfg: on the ESP32 this handler runs in parallel to loop(),
    // and the following restart reloads config.json anyway.
    req->send(200, "text/plain", "OK");
    restartAt = millis() + 1500;
}
