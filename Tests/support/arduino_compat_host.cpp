// Host implementation of the Arduino shim SdFat references (the production
// components/SdFat/port/arduino_compat.cpp pulls ESP-IDF/FreeRTOS headers that
// don't exist on a host build). For unit tests we only need millis/micros/etc;
// the GPIO functions are no-ops (no SD-over-SPI on the host — tests use a
// RAM-backed FsBlockDeviceInterface, not SdSpiCard).
#include "arduino_compat.h"
#include <chrono>

static uint64_t nowUs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

unsigned long millis(void) { return (unsigned long)(nowUs() / 1000); }
unsigned long micros(void) { return (unsigned long)nowUs(); }
void delay(unsigned long) {}
void delayMicroseconds(unsigned int) {}
void yield(void) {}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int  digitalRead(uint8_t) { return 0; }
