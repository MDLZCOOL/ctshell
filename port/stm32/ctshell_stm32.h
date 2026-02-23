/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "ctshell.h"
#include "usart.h"

#ifdef __cplusplus
extern "C" {
#endif

void ctshell_port_stm32_init(ctshell_ctx_t *ctx, UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif
