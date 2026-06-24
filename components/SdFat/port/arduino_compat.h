// Minimal Arduino-compat shim so SdFat builds on ESP-IDF with
// ENABLE_ARDUINO_FEATURES=0. Force-included into every SdFat TU.
#pragma once
#include <stdint.h>
#include <stddef.h>

// Arduino flash-string helper: SdFat only uses `const __FlashStringHelper*` and
// reinterpret_casts it to const char* on non-AVR, so a forward decl suffices.
#ifdef __cplusplus
class __FlashStringHelper;
#endif

#ifdef __cplusplus
extern "C" {
#endif
unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield(void);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int  digitalRead(uint8_t pin);
#ifdef __cplusplus
}
#endif

#ifndef SS
#define SS 0xFF   // Arduino default CS pin; unused — we always pass an explicit CS.
#endif
#ifndef HIGH
#define HIGH 1
#define LOW  0
#endif
#ifndef OUTPUT
#define OUTPUT 0x03
#define INPUT  0x01
#endif
