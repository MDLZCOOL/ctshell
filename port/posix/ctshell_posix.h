/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "ctshell.h"

#ifdef __cplusplus
extern "C" {
#endif

int ctshell_posix_init(ctshell_ctx_t *ctx);
void ctshell_posix_deinit(ctshell_ctx_t *ctx);
void ctshell_posix_process_input(ctshell_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
