#include "uart_display_proto.h"
#include "esp_log.h"

static const char *TAG = "uart_proto";

void uart_display_proto_init() {
    ESP_LOGI(TAG, "UART display protocol init (stub)");
    // TODO: init UART, rx buffer, register callbacks
}

void uart_display_proto_poll() {
    // TODO: non-blocking read/parse and dispatch to UI bindings
}
