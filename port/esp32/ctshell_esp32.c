/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell_esp32.h"
#include "ctshell.h"

#include <string.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

typedef struct {
    uart_port_t uart_num;
    ctshell_ctx_t ctx;
} ctshell_esp32_priv_t;

static ctshell_esp32_priv_t priv;

static void shell_write(const char *str, uint16_t len, void *p) {
    ctshell_esp32_priv_t *obj = p;
    uart_write_bytes(obj->uart_num, str, len);
}

static uint32_t shell_get_tick(void) {
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
}

static void shell_rx_task(void *arg) {
    ctshell_esp32_priv_t *obj = arg;
    uint8_t ch;

    while (1) {
        int len = uart_read_bytes(obj->uart_num, &ch, 1, portMAX_DELAY);

        if (len > 0) {
            ctshell_input(&obj->ctx, ch);
        }
    }
}

static void shell_task(void *arg) {
    ctshell_esp32_priv_t *obj = arg;

    while (1) {
        ctshell_poll(&obj->ctx);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ctshell_esp32_init(void) {
    memset(&priv, 0, sizeof(priv));
    priv.uart_num = CONFIG_CTSHELL_ESP32_UART_NUM;

    const uart_config_t uart_config =
            {
                    .baud_rate = CONFIG_CTSHELL_ESP32_UART_BAUD,
                    .data_bits = UART_DATA_8_BITS,
                    .parity    = UART_PARITY_DISABLE,
                    .stop_bits = UART_STOP_BITS_1,
                    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                    .source_clk = UART_SCLK_DEFAULT,
            };
    uart_driver_install(priv.uart_num, CONFIG_CTSHELL_ESP32_RX_BUF_SIZE, 0, 0, NULL, 0);
    uart_param_config(priv.uart_num, &uart_config);
    uart_set_pin(
            priv.uart_num,
            CONFIG_CTSHELL_ESP32_UART_TX,
            CONFIG_CTSHELL_ESP32_UART_RX,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
    );
    ctshell_io_t io =
            {
                    .write = shell_write,
                    .get_tick = shell_get_tick,
            };
    ctshell_init(&priv.ctx, io, &priv);

    xTaskCreate(shell_rx_task, "ctshell_rx", CONFIG_CTSHELL_ESP32_RX_TASK_STACK, &priv, 5, NULL);
    xTaskCreate(shell_task, "ctshell", CONFIG_CTSHELL_ESP32_TASK_STACK, &priv, CONFIG_CTSHELL_ESP32_TASK_PRIO, NULL);
}
