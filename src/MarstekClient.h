#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Operating modes as also transmitted to homee (order = homee value)
enum class VenusMode : uint8_t
{
    Auto = 0,
    AI = 1,
    UPS = 2,
    Manual = 3,
    Passive = 4,
    Unknown = 255
};

const char *venusModeName(VenusMode m);
VenusMode venusModeFromName(const char *name);

// Last read state of the battery
struct VenusState
{
    bool online = false;               // last requests successful
    uint32_t changeCounter = 0;        // incremented on every new response
    unsigned long lastResponse = 0;    // millis()
    uint32_t okCount = 0;
    uint32_t errCount = 0;

    // Marstek.GetDevice
    String device;
    int fwVersion = 0;
    String bleMac;

    // ES.GetStatus
    bool esValid = false;
    int soc = 0;                       // %
    int batCapWh = 0;                  // remaining energy Wh (E mini) or capacity
    int batPower = 0;                  // W, + charging / - discharging
    bool batPowerCalculated = false;   // bat_power missing -> calculated
    int pvPower = 0;
    int ongridPower = 0;               // W, + feed-in/AC output / - charging from grid
    int offgridPower = 0;
    uint32_t gridOutWh = 0;            // total_grid_output_energy
    uint32_t gridInWh = 0;             // total_grid_input_energy
    uint32_t loadWh = 0;               // total_load_energy

    // ES.GetMode
    bool modeValid = false;
    VenusMode mode = VenusMode::Unknown;

    // EM.GetStatus (CT meter)
    bool emValid = false;
    int ctState = 0;
    int emTotalPower = 0;
    int emA = 0, emB = 0, emC = 0;

    // Bat.GetStatus
    bool batValid = false;
    float batTemp = 0;
    float ratedCapWh = 0;
    bool chargeAllowed = false;
    bool dischargeAllowed = false;
};

// Non-blocking client for the Marstek Open API (UDP JSON-RPC).
// At most one request is in flight at any time; there is a pause between requests.
class MarstekClient
{
public:
    void begin(const String &ip, uint16_t port, uint16_t pollIntervalSec);
    void loop();

    // Request a mode change (Auto, AI, UPS). Sent in the next free slot.
    bool requestMode(VenusMode mode);
    bool modeChangePending() const { return wantedMode != VenusMode::Unknown; }

    const VenusState &state() const { return st; }
    bool isConfigured() const { return configured; }
    const String &lastRaw() const { return lastRawResponse; }

private:
    enum class Req : uint8_t
    {
        None,
        GetDevice,
        EsStatus,
        EsMode,
        EmStatus,
        BatStatus,
        SetMode
    };

    void scheduleCycle();
    void sendRequest(Req r);
    void transmit();
    void handlePacket();
    void handleResult(Req r, JsonObject result);
    void requestFailed();
    const char *reqMethod(Req r) const;

    WiFiUDP udp;
    IPAddress target;
    uint16_t port = 30000;
    bool configured = false;
    unsigned long pollIntervalMs = 30000;

    VenusState st;

    // queue for the current polling cycle
    Req queue[8];
    uint8_t qHead = 0, qLen = 0;
    uint32_t cycleCount = 0;
    unsigned long lastCycle = 0;

    // request in flight
    Req pending = Req::None;
    uint32_t pendingId = 0;
    uint32_t nextId = 1;
    uint8_t retransmits = 0;
    unsigned long sentAt = 0;
    unsigned long lastActivity = 0;
    uint8_t consecutiveFails = 0;
    String txBuf;

    VenusMode wantedMode = VenusMode::Unknown;
    String lastRawResponse;
};
