#include "HomeeBridge.h"

#include <virtualHomee.hpp>
#include <virtualHomee/homee_defines.h>
#include <virtualHomee/homee_icons.h>

#ifndef VB_VERSION
#define VB_VERSION "0"
#endif

static const uint32_t NODE_ID = 10;

HomeeBridge *HomeeBridge::instance = nullptr;
volatile int HomeeBridge::pendingSwitch = -1;

// Mode switches in homee, in the order of the Marstek app
static const VenusMode SWITCH_MODES[] = {VenusMode::Auto, VenusMode::AI, VenusMode::UPS, VenusMode::Manual};
static const char *const SWITCH_NAMES[] = {"Eigenverbrauch", "KI-Optimierung", "UPS", "Manuell"};

// Set the ID in the constructor (fixed IDs, so homee recognizes the attributes after updates).
// Attribute names are intentionally German (shown in the homee app).
static nodeAttributes *addAttr(node *n, uint16_t type, uint32_t id, const char *name, const char *unit, double min, double max, double step = 1)
{
    nodeAttributes *a = new nodeAttributes(type, id);
    a->setName(name);
    a->setUnit(unit);
    a->setMinimumValue(min);
    a->setMaximumValue(max);
    a->setStepValue(step);
    a->setCallback(nullptr);
    return n->AddAttributes(a);
}

void HomeeBridge::begin(const String &vhihName, const String &deviceName, MarstekClient &client)
{
    mc = &client;
    instance = this;
    vhih = new virtualHomee(vhihName);

    dev = new node(NODE_ID, CANodeProfileEnergyMeter, deviceName);
    dev->setImage(NodeIconEnergyMeter);
    uint32_t id = NODE_ID * 100;
    aSoc = addAttr(dev, CAAttributeTypeBatteryLevel, ++id, "Ladezustand", "%", 0, 100);
    aBatPower = addAttr(dev, CAAttributeTypeCurrentEnergyUse, ++id, "Batterieleistung", "W", -3000, 3000);
    aCharge = addAttr(dev, CAAttributeTypePowerInputBattery, ++id, "Laden", "W", 0, 3000);
    aDischarge = addAttr(dev, CAAttributeTypePowerOutputBattery, ++id, "Entladen", "W", 0, 3000);
    aGrid = addAttr(dev, CAAttributeTypePowerOutputGrid, ++id, "Netzleistung", "W", -3000, 3000);
    aEnergy = addAttr(dev, CAAttributeTypeAccumulatedEnergyUse, ++id, "Energieinhalt", "kWh", 0, 10, 0.01);
    aGridOut = addAttr(dev, CAAttributeTypeTotalEnergyOutputGrid, ++id, "Einspeisung gesamt", "kWh", 0, 1000000, 0.01);
    aGridIn = addAttr(dev, CAAttributeTypeTotalEnergyInputGrid, ++id, "Bezug gesamt", "kWh", 0, 1000000, 0.01);
    aTemp = addAttr(dev, CAAttributeTypeDeviceTemperature, ++id, "Temperatur", "°C", -20, 80, 0.1);
    aCtPower = addAttr(dev, CAAttributeTypeTotalCurrentEnergyUse, ++id, "Hausverbrauch (CT)", "W", -10000, 10000);
    ++id;   // 1011 was the former CAAttributeTypeMode attribute, do not reuse
    for (uint8_t i = 0; i < MODE_SWITCHES; i++)
    {
        aModeSw[i] = addAttr(dev, CAAttributeTypeOnOff, ++id, SWITCH_NAMES[i], "", 0, 1);
        aModeSw[i]->setInstance(i + 1);
        aModeSw[i]->setEditable(1);
        aModeSw[i]->setCallback(&HomeeBridge::onModeSwitch);
    }
    // Firmware versions as numbers (bridge: VB_VERSION "0.06" -> 0.06, battery: "ver" from Marstek.GetDevice)
    nodeAttributes *aBridgeFw = addAttr(dev, CAAttributeTypeFirmwareRevision, ++id, "Firmware Bridge", "", 0, 100, 0.01);
    aBridgeFw->setInstance(1);
    aBridgeFw->setCurrentValue(atof(VB_VERSION));
    aBridgeFw->setTargetValue(atof(VB_VERSION));
    aBatFw = addAttr(dev, CAAttributeTypeFirmwareRevision, ++id, "Firmware Speicher", "", 0, 100000);
    aBatFw->setInstance(2);

    vhih->addNode(dev);
    vhih->start();
    Serial.printf("[HOM] virtual homee '%s' started (port 7681), device '%s'\n", vhihName.c_str(), deviceName.c_str());
}

// Called from the WebSocket context -> only remember, processing happens in loop()
void HomeeBridge::onModeSwitch(nodeAttributes *a)
{
    if (!instance)
        return;
    for (uint8_t i = 0; i < MODE_SWITCHES; i++)
        if (instance->aModeSw[i] == a)
            pendingSwitch = i * 2 + (a->getTargetValue() >= 0.5 ? 1 : 0);
}

// Radio-button logic: exactly one switch is on (all off only in the API-only mode "Passive")
void HomeeBridge::handleModeSwitch(int index, bool on)
{
    VenusMode wanted = SWITCH_MODES[index];
    if (on && mc->requestMode(wanted))
    {
        publishMode(wanted);   // optimistic, confirmed by the read-back of ES.GetMode
        return;
    }
    if (on)
        Serial.printf("[HOM] mode %s cannot be set directly\n", venusModeName(wanted));
    // switching off the active mode or a mode that cannot be set -> back to the actual mode
    const VenusState &s = mc->state();
    publishMode(s.modeValid ? s.mode : VenusMode::Unknown);
}

void HomeeBridge::publishMode(VenusMode mode)
{
    for (uint8_t i = 0; i < MODE_SWITCHES; i++)
        publish(aModeSw[i], SWITCH_MODES[i] == mode ? 1 : 0);
}

size_t HomeeBridge::clientCount() const
{
    return vhih ? vhih->getNumberOfWSClients() : 0;
}

void HomeeBridge::publish(nodeAttributes *a, double value)
{
    if (a->getCurrentValue() == value && a->getTargetValue() == value)
        return;
    a->setTargetValue(value);
    a->setCurrentValue(value);
    vhih->updateAttribute(a);
}

void HomeeBridge::loop()
{
    if (!vhih)
        return;

    int sw = pendingSwitch;
    if (sw >= 0)
    {
        pendingSwitch = -1;
        handleModeSwitch(sw / 2, sw % 2);
    }

    const VenusState &s = mc->state();

    // Mode switches: not while a mode change is still pending (avoids flickering back to
    // the old mode). Checked on every pass, so a rejected/failed change is reverted as well.
    if (s.modeValid && !mc->modeChangePending())
        publishMode(s.mode);

    if (s.changeCounter == lastChange)
        return;
    lastChange = s.changeCounter;

    if (s.esValid)
    {
        publish(aSoc, s.soc);
        publish(aBatPower, s.batPower);
        publish(aCharge, s.batPower > 0 ? s.batPower : 0);
        publish(aDischarge, s.batPower < 0 ? -s.batPower : 0);
        publish(aGrid, s.ongridPower);
        publish(aEnergy, s.batCapWh / 1000.0);
        publish(aGridOut, s.gridOutWh / 1000.0);
        publish(aGridIn, s.gridInWh / 1000.0);
    }
    if (s.batValid)
        publish(aTemp, s.batTemp);
    if (s.emValid)
        publish(aCtPower, s.emTotalPower);
    if (s.fwVersion > 0)
        publish(aBatFw, s.fwVersion);
}
