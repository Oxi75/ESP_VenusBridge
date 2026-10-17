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
    void publishMode(VenusMode mode);
    void handleModeSwitch(int index, bool on);
    static void onModeSwitch(nodeAttributes *a);

    static const uint8_t MODE_SWITCHES = 4;

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
    nodeAttributes *aModeSw[MODE_SWITCHES] = {};   // one on/off switch per mode (radio buttons)

    static HomeeBridge *instance;
    static volatile int pendingSwitch;   // switch event from homee: index * 2 + on, -1 = none
};
