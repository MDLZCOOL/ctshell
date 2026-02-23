/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell_stm32.h"
#include <string.h>

typedef struct {
    UART_HandleTypeDef *huart;
} ctshell_stm32_priv_t;

static ctshell_ctx_t *g_ctx;
static uint8_t rx_byte;
static ctshell_stm32_priv_t priv;

static void stm32_shell_write(const char *str, uint16_t len, void *p) {
    ctshell_stm32_priv_t *d = (ctshell_stm32_priv_t *) p;
    HAL_UART_Transmit(d->huart, (uint8_t *) str, len, 100);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == priv.huart) {
        ctshell_input(g_ctx, rx_byte);
        HAL_UART_Receive_IT(priv.huart, &rx_byte, 1);
    }
}

void ctshell_port_stm32_init(ctshell_ctx_t *ctx, UART_HandleTypeDef *huart) {
    memset(&priv, 0, sizeof(priv));
    priv.huart = huart;
    g_ctx = ctx;

    ctshell_io_t io = {
            .write = stm32_shell_write,
            .get_tick = HAL_GetTick,
    };

    ctshell_init(ctx, io, &priv);
    HAL_UART_Receive_IT(huart, &rx_byte, 1);
}
