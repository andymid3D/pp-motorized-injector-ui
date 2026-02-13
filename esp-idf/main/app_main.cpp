#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ui_bindings.h"
#include "uart_display_proto.h"

static const char *TAG = "ui_app";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "UI app starting (ESP-IDF scaffolding)");

    ui_bindings_init();
    uart_display_proto_init();

    while (true) {
        uart_display_proto_poll();
        ui_bindings_tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
