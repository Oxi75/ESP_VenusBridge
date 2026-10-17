#pragma once

#include <Arduino.h>
#include "MarstekClient.h"

class virtualHomee;
class node;
class nodeAttributes;

// Exposes the battery as a device in homee ("homee in homee")
class HomeeBridge
{
public:
    // vhihName = virtual homee name (homee ID when pairing), deviceName = homee device name
    void begin(const String &vhihName, const String &deviceName, MarstekClient &client);
    void loop();

    size_t clientCount() const;

private:
    void publish(nodeAttributes *a, double value);
    static void onModeChanged(nodeAttributes *a);

    virtualHomee *vhih = nullptr;
    node *dev = nullptr;
    MarstekClient *mc = nullptr;
    uint32_t lastChange = 0;

    nodeAttributes *aSoc = nullptr;
    nodeAttributes *aBatPower = nullptr;
    nodeAttributes *aCharge = nullptr;
    nodeAttributes *aDischarge = nullptr;
    nodeAttributes *aGrid = nullptr;
    nodeAttributes *aEnergy = nullptr;
    nodeAttributes *aGridOut = nullptr;
    nodeAttributes *aGridIn = nullptr;
    nodeAttributes *aTemp = nullptr;
    nodeAttributes *aCtPower = nullptr;
    nodeAttributes *aMode = nullptr;

    static volatile int pendingMode;   // mode set by homee, -1 = none
};
