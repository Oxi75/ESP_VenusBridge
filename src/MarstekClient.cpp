#include "MarstekClient.h"
#include "StateLock.h"

// Timing according to the Open API / experience from the Home Assistant integration
static const unsigned long REQUEST_GAP_MS = 3000;          // pause between two requests
static const unsigned long REQUEST_TIMEOUT_MS = 10000;     // no response -> error
static const unsigned long RETRANSMIT_MS[] = {500, 2500, 5000};
static const uint8_t FAILS_UNTIL_OFFLINE = 3;
static const uint32_t SLOW_POLL_EVERY = 10;                // Bat/Device only every n-th cycle
static const size_t RX_BUF_SIZE = 768;                     // max. size of a received datagram

static const char *MODE_NAMES[] = {"Auto", "AI", "UPS", "Manual", "Passive"};

const char *venusModeName(VenusMode m)
{
    uint8_t i = (uint8_t)m;
    return i < 5 ? MODE_NAMES[i] : "Unknown";
}

VenusMode venusModeFromName(const char *name)
{
    if (!name)
        return VenusMode::Unknown;
    for (uint8_t i = 0; i < 5; i++)
        if (strcasecmp(name, MODE_NAMES[i]) == 0)
            return (VenusMode)i;
    return VenusMode::Unknown;
}

// Some firmwares send small negative values as unsigned 16-bit numbers
static int fixSigned16(long v)
{
    return (v > 32767 && v < 65536) ? (int)(v - 65536) : (int)v;
}

void MarstekClient::begin(const String &ip, uint16_t p, uint16_t pollIntervalSec)
{
    configured = target.fromString(ip) && p != 0;
    port = p;
    pollIntervalMs = (unsigned long)pollIntervalSec * 1000UL;
    if (!configured)
    {
        Serial.println(F("[MST] no valid Marstek IP configured"));
        return;
    }
    // Some firmwares reply to the destination port instead of the source port ->
    // listen locally on the same port and send from there.
    udp.begin(port);
    txBuf.reserve(200);
    lastRawResponse.reserve(RX_BUF_SIZE);
    lastCycle = millis() - pollIntervalMs;   // start the first cycle immediately
    Serial.printf("[MST] target %s:%u, interval %lus\n", target.toString().c_str(), port, pollIntervalMs / 1000);
}

bool MarstekClient::requestMode(VenusMode mode)
{
    // Manual is intentionally not supported: switching to Manual via the Open API overwrites
    // the schedules configured in the Marstek app (even without manual_cfg, tested in 0.04).
    if (mode != VenusMode::Auto && mode != VenusMode::AI && mode != VenusMode::UPS)
        return false;
    wantedMode = mode;
    Serial.printf("[MST] mode change requested: %s\n", venusModeName(mode));
    return true;
}

void MarstekClient::scheduleCycle()
{
    qHead = 0;
    qLen = 0;
    bool slow = (cycleCount % SLOW_POLL_EVERY) == 0;
    if (slow || st.device.length() == 0)
        queue[qLen++] = Req::GetDevice;
    queue[qLen++] = Req::EsStatus;
    queue[qLen++] = Req::EsMode;
    queue[qLen++] = Req::EmStatus;
    if (slow)
        queue[qLen++] = Req::BatStatus;
    cycleCount++;
}

const char *MarstekClient::reqMethod(Req r) const
{
    switch (r)
    {
    case Req::GetDevice: return "Marstek.GetDevice";
    case Req::EsStatus: return "ES.GetStatus";
    case Req::EsMode: return "ES.GetMode";
    case Req::EmStatus: return "EM.GetStatus";
    case Req::BatStatus: return "Bat.GetStatus";
    case Req::SetMode: return "ES.SetMode";
    default: return "";
    }
}

void MarstekClient::sendRequest(Req r)
{
    pending = r;
    pendingId = nextId++;
    retransmits = 0;

    txBuf = F("{\"id\":");
    txBuf += pendingId;
    txBuf += F(",\"method\":\"");
    txBuf += reqMethod(r);
    txBuf += F("\",\"params\":");
    if (r == Req::GetDevice)
    {
        txBuf += F("{\"ble_mac\":\"0\"}");
    }
    else if (r == Req::SetMode)
    {
        const char *cfgKey = wantedMode == VenusMode::AI ? "ai_cfg" : wantedMode == VenusMode::UPS ? "ups_cfg" : "auto_cfg";
        txBuf += F("{\"id\":0,\"config\":{\"mode\":\"");
        txBuf += venusModeName(wantedMode);
        txBuf += F("\",\"");
        txBuf += cfgKey;
        txBuf += F("\":{\"enable\":1}}}");
    }
    else
    {
        txBuf += F("{\"id\":0}");
    }
    txBuf += '}';

    transmit();
}

void MarstekClient::transmit()
{
    udp.beginPacket(target, port);
    udp.write((const uint8_t *)txBuf.c_str(), txBuf.length());
    udp.endPacket();
    sentAt = millis();
    lastActivity = sentAt;
    if (retransmits == 0)
        Serial.printf("[MST] -> %s\n", txBuf.c_str());
}

void MarstekClient::loop()
{
    if (!configured)
        return;

    if (udp.parsePacket() > 0)
        handlePacket();

    unsigned long now = millis();

    if (pending != Req::None)
    {
        unsigned long age = now - sentAt;
        // retransmit with the same ID (older firmware < 150 does not handle this well)
        bool fwAllowsRetransmit = st.fwVersion == 0 || st.fwVersion >= 150;
        if (fwAllowsRetransmit && retransmits < 3 && age >= RETRANSMIT_MS[retransmits] &&
            pending != Req::SetMode)
        {
            unsigned long origSent = sentAt;
            retransmits++;
            transmit();
            sentAt = origSent;   // timeout counts from the first transmission
        }
        if (now - sentAt >= REQUEST_TIMEOUT_MS)
            requestFailed();
        return;
    }

    if (now - lastActivity < REQUEST_GAP_MS)
        return;

    if (wantedMode != VenusMode::Unknown)
    {
        sendRequest(Req::SetMode);
        return;
    }

    if (qLen == 0 && now - lastCycle >= pollIntervalMs)
    {
        lastCycle = now;
        scheduleCycle();
    }

    if (qLen > 0)
    {
        Req r = queue[qHead++];
        qLen--;
        sendRequest(r);
    }
}

void MarstekClient::requestFailed()
{
    Serial.printf("[MST] timeout on %s\n", reqMethod(pending));
    StateLock lock;
    if (pending == Req::SetMode)
        wantedMode = VenusMode::Unknown;   // do not retry endlessly
    pending = Req::None;
    lastActivity = millis();
    st.errCount++;
    if (++consecutiveFails >= FAILS_UNTIL_OFFLINE && st.online)
    {
        st.online = false;
        st.changeCounter++;
        Serial.println(F("[MST] battery not reachable"));
    }
    // Backoff while not reachable: drop the rest of the cycle, the first request
    // of the next cycle (after the polling interval) acts as probe
    if (!st.online && qLen > 0)
    {
        Serial.printf("[MST] battery not reachable, skipping %u requests of this cycle\n", (unsigned)qLen);
        qHead = 0;
        qLen = 0;
    }
}

void MarstekClient::handlePacket()
{
    char buf[RX_BUF_SIZE];
    IPAddress from = udp.remoteIP();
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0)
        return;
    buf[len] = 0;
    if (from != target)
        return;

    // tolerate the typo from the specification ("set_result":ture)
    for (char *p = strstr(buf, ":ture"); p; p = strstr(p + 1, ":ture"))
        memcpy(p, ":true", 5);

    Serial.printf("[MST] <- %s\n", buf);
    StateLock lock;
    lastRawResponse = buf;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf, len);
    if (err)
    {
        Serial.printf("[MST] JSON error: %s\n", err.c_str());
        return;
    }

    uint32_t id = doc["id"] | 0;
    if (pending == Req::None || id != pendingId)
        return;   // late response to a request that has already been completed

    Req r = pending;
    pending = Req::None;
    lastActivity = millis();

    if (!doc["error"].isNull())
    {
        Serial.printf("[MST] error %d: %s\n", (int)(doc["error"]["code"] | 0), (const char *)(doc["error"]["message"] | ""));
        if (r == Req::SetMode)
            wantedMode = VenusMode::Unknown;
        st.errCount++;
        return;
    }

    JsonObject result = doc["result"];
    if (result.isNull())
        return;

    consecutiveFails = 0;
    st.okCount++;
    st.lastResponse = lastActivity;
    if (!st.online)
    {
        st.online = true;
        Serial.println(F("[MST] battery reachable"));
    }
    handleResult(r, result);
    st.changeCounter++;
}

void MarstekClient::handleResult(Req r, JsonObject res)
{
    switch (r)
    {
    case Req::GetDevice:
        st.device = res["device"] | "";
        st.fwVersion = res["ver"] | 0;
        st.bleMac = res["ble_mac"] | "";
        break;

    case Req::EsStatus:
    {
        int soc = res["bat_soc"] | 0;
        int cap = res["bat_cap"] | 0;
        // BMS silent -> firmware returns zeros; keep the old values in that case
        if (soc == 0 && cap == 0)
        {
            Serial.println(F("[MST] ES.GetStatus without valid battery data"));
            break;
        }
        st.soc = soc;
        st.batCapWh = cap;
        st.pvPower = fixSigned16(res["pv_power"] | 0L);
        st.ongridPower = fixSigned16(res["ongrid_power"] | 0L);
        st.offgridPower = fixSigned16(res["offgrid_power"] | 0L);
        if (res["bat_power"].is<long>())
        {
            st.batPower = fixSigned16(res["bat_power"].as<long>());
            st.batPowerCalculated = false;
        }
        else
        {
            st.batPower = st.pvPower - st.ongridPower - st.offgridPower;
            st.batPowerCalculated = true;
        }
        st.gridOutWh = res["total_grid_output_energy"] | 0UL;
        st.gridInWh = res["total_grid_input_energy"] | 0UL;
        st.loadWh = res["total_load_energy"] | 0UL;
        st.esValid = true;
        break;
    }

    case Req::EsMode:
        st.mode = venusModeFromName(res["mode"] | "");
        st.modeValid = true;
        break;

    case Req::EmStatus:
        st.ctState = res["ct_state"] | 0;
        st.emA = fixSigned16(res["a_power"] | 0L);
        st.emB = fixSigned16(res["b_power"] | 0L);
        st.emC = fixSigned16(res["c_power"] | 0L);
        st.emTotalPower = fixSigned16(res["total_power"] | 0L);
        st.emValid = true;
        break;

    case Req::BatStatus:
        st.batTemp = res["bat_temp"] | 0.0f;
        st.ratedCapWh = res["rated_capacity"] | 0.0f;
        st.chargeAllowed = res["charg_flag"] | false;
        st.dischargeAllowed = res["dischrg_flag"] | false;
        st.batValid = true;
        break;

    case Req::SetMode:
    {
        bool ok = res["set_result"] | false;
        Serial.printf("[MST] mode change to %s %s\n", venusModeName(wantedMode), ok ? "OK" : "rejected");
        if (ok)
            st.mode = wantedMode;
        wantedMode = VenusMode::Unknown;
        // read back the result for verification
        if (qLen < sizeof(queue) / sizeof(queue[0]))
        {
            if (qHead > 0)
                queue[--qHead] = Req::EsMode;
            else
            {
                memmove(&queue[1], &queue[0], qLen * sizeof(Req));
                queue[0] = Req::EsMode;
            }
            qLen++;
        }
        break;
    }

    default:
        break;
    }
}
