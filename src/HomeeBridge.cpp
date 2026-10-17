#include "HomeeBridge.h"

#include <virtualHomee.hpp>
#include <virtualHomee/homee_defines.h>
#include <virtualHomee/homee_icons.h>

static const uint32_t NODE_ID = 10;

volatile int HomeeBridge::pendingMode = -1;

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
    aMode = addAttr(dev, CAAttributeTypeMode, ++id, "Betriebsmodus", "", 0, 4);
    aMode->setEditable(1);
    aMode->setCallback(&HomeeBridge::onModeChanged);

    vhih->addNode(dev);
    vhih->start();
    Serial.printf("[HOM] virtual homee '%s' started (port 7681), device '%s'\n", vhihName.c_str(), deviceName.c_str());
}

// Called from the WebSocket context -> only remember, processing happens in loop()
void HomeeBridge::onModeChanged(nodeAttributes *a)
{
    pendingMode = (int)(a->getTargetValue() + 0.5);
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

    int m = pendingMode;
    if (m >= 0)
    {
        pendingMode = -1;
        VenusMode wanted = (VenusMode)m;
        if (!mc->requestMode(wanted))
        {
            Serial.printf("[HOM] mode %d cannot be set directly\n", m);
            // reset homee to the actual mode
            aMode->setTargetValue(aMode->getCurrentValue());
            vhih->updateAttribute(aMode);
        }
    }

    const VenusState &s = mc->state();
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
    if (s.modeValid && s.mode != VenusMode::Unknown)
        publish(aMode, (double)(uint8_t)s.mode);
}
