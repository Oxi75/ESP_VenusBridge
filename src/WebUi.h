#pragma once

#include <Arduino.h>
#include "Config.h"

class AsyncWebServer;
class AsyncWebServerRequest;
class NetManager;
class MarstekClient;
class HomeeBridge;

// Web interface on port 80: status, settings, firmware update
class WebUi
{
public:
    void begin(AppConfig &cfg, NetManager &net, MarstekClient &mc, HomeeBridge &homee);
    void loop();

private:
    void handleStatus(AsyncWebServerRequest *req);
    void handleGetConfig(AsyncWebServerRequest *req);
    void handleSetConfig(AsyncWebServerRequest *req);

    AsyncWebServer *server = nullptr;
    AppConfig *cfg = nullptr;
    NetManager *net = nullptr;
    MarstekClient *mc = nullptr;
    HomeeBridge *homee = nullptr;
    unsigned long restartAt = 0;
};
