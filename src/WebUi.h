#pragma once

#include <Arduino.h>
#include "Config.h"

class AsyncWebServer;
class AsyncWebServerRequest;
class NetManager;
class MarstekClient;
class HomeeBridge;

// Web interface on port 80: status, settings, firmware update.
// Saving and firmware update are protected by the optional admin password
// (HTTP header X-Auth), except for requests arriving via the access point.
class WebUi
{
public:
    void begin(AppConfig &cfg, NetManager &net, MarstekClient &mc, HomeeBridge &homee);
    void loop();

private:
    enum class Auth { Ok, Denied, Blocked };

    void handleStatus(AsyncWebServerRequest *req);
    void handleGetConfig(AsyncWebServerRequest *req);
    void handleSetConfig(AsyncWebServerRequest *req);

    bool isViaAp(AsyncWebServerRequest *req) const;
    bool isAuthRequired(AsyncWebServerRequest *req) const;
    Auth authState(AsyncWebServerRequest *req) const;
    bool checkAuth(AsyncWebServerRequest *req);   // sends 401/429 and returns false if not authorized

    AsyncWebServer *server = nullptr;
    AppConfig *cfg = nullptr;
    NetManager *net = nullptr;
    MarstekClient *mc = nullptr;
    HomeeBridge *homee = nullptr;
    unsigned long restartAt = 0;
    unsigned long authBlockedUntil = 0;   // brute-force brake after a wrong password
    bool uploadAuthorized = false;
};
