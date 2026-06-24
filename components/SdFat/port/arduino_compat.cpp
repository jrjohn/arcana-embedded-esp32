#include "arduino_compat.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

unsigned long millis(void) { return (unsigned long)(esp_timer_get_time() / 1000); }
unsigned long micros(void) { return (unsigned long)esp_timer_get_time(); }
void delay(unsigned long ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
void delayMicroseconds(unsigned int us) { ets_delay_us(us); }
void yield(void) { taskYIELD(); }

void pinMode(uint8_t pin, uint8_t mode) {
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << pin;
    c.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    c.pull_up_en = GPIO_PULLUP_DISABLE;
    c.pull_down_en = GPIO_PULLDOWN_DISABLE;
    c.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&c);
}
void digitalWrite(uint8_t pin, uint8_t val) { gpio_set_level((gpio_num_t)pin, val); }
int  digitalRead(uint8_t pin) { return gpio_get_level((gpio_num_t)pin); }

// SdFat's own SdSpiChipSelect.cpp is gated behind ENABLE_ARDUINO_FEATURES (off
// here), so we provide the CS helpers SdSpiCard links against. SdCsPin_t==uint8_t.
extern "C++" void sdCsInit(uint8_t pin) { pinMode(pin, OUTPUT); }
extern "C++" void sdCsWrite(uint8_t pin, bool level) { digitalWrite(pin, level ? HIGH : LOW); }
