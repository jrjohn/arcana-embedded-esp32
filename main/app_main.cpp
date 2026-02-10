/*
 * Arcana ESP32 Application Entry Point
 *
 * All service wiring is handled by Controller.
 */

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "Controller.hpp"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    Arcana::Controller::getInstance().run();
}
