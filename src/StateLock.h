#pragma once

#include <Arduino.h>

// Protects the Marstek state against concurrent access.
// ESP32: web server handlers run in the async_tcp task, in parallel to loop() -> mutex.
// ESP8266: everything runs in the same context -> no lock needed.
#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class StateLock
{
public:
    StateLock() { xSemaphoreTake(mutex(), portMAX_DELAY); }
    ~StateLock() { xSemaphoreGive(mutex()); }
    StateLock(const StateLock &) = delete;
    StateLock &operator=(const StateLock &) = delete;

private:
    static SemaphoreHandle_t mutex()
    {
        static SemaphoreHandle_t m = xSemaphoreCreateMutex();
        return m;
    }
};
#else
class StateLock
{
public:
    StateLock() {}
};
#endif
