/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "ctshell.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ctshell_port_cherryusb_init(ctshell_ctx_t *ctx, uint8_t busid, uintptr_t reg_base);

#ifdef __cplusplus
}
#endif
