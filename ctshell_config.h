/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/* ================= Feature Options ================= */
#define CTSHELL_USE_BUILTIN_CMDS
// #define CTSHELL_USE_DOUBLE
// #define CTSHELL_USE_FS
// #define CTSHELL_USE_FS_FATFS

/* ================= Resource Limits ================= */
#define CTSHELL_CMD_NAME_MAX_LEN    16
#define CTSHELL_LINE_BUF_SIZE       128
#define CTSHELL_MAX_ARGS            16
#define CTSHELL_HISTORY_SIZE        5
#define CTSHELL_VAR_MAX_COUNT       8
#define CTSHELL_VAR_NAME_LEN        16
#define CTSHELL_VAR_VAL_LEN         32
#define CTSHELL_FIFO_SIZE           128
#ifdef CTSHELL_USE_FS
#define CTSHELL_FS_PATH_MAX         256
#define CTSHELL_FS_NAME_MAX         64
#endif
#define CTSHELL_PROMPT              "ctsh>> "
