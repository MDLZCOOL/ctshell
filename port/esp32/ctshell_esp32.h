/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "ctshell.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_CTSHELL_ESP32_UART_NUM
#define CONFIG_CTSHELL_ESP32_UART_NUM      UART_NUM_2
#endif

#ifndef CONFIG_CTSHELL_ESP32_UART_TX
#define CONFIG_CTSHELL_ESP32_UART_TX       9
#endif

#ifndef CONFIG_CTSHELL_ESP32_UART_RX
#define CONFIG_CTSHELL_ESP32_UART_RX       10
#endif

#ifndef CONFIG_CTSHELL_ESP32_UART_BAUD
#define CONFIG_CTSHELL_ESP32_UART_BAUD     115200
#endif

#ifndef CONFIG_CTSHELL_ESP32_RX_BUF_SIZE
#define CONFIG_CTSHELL_ESP32_RX_BUF_SIZE   1024
#endif

#ifndef CONFIG_CTSHELL_ESP32_RX_TASK_STACK
#define CONFIG_CTSHELL_ESP32_RX_TASK_STACK 2048
#endif

#ifndef CONFIG_CTSHELL_ESP32_TASK_STACK
#define CONFIG_CTSHELL_ESP32_TASK_STACK    4096
#endif

#ifndef CONFIG_CTSHELL_ESP32_TASK_PRIO
#define CONFIG_CTSHELL_ESP32_TASK_PRIO     5
#endif

void ctshell_esp32_init(void);

#ifdef __cplusplus
}
#endif
