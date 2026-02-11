/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifndef CONFIG_CTSHELL_USE_KCONFIG

/* ================= Feature Options ================= */
#define CONFIG_CTSHELL_USE_BUILTIN_CMDS
//#define CONFIG_CTSHELL_USE_DOUBLE
//#define CONFIG_CTSHELL_USE_FS
//#define CONFIG_CTSHELL_USE_FS_FATFS

/* ================= Resource Limits ================= */
#define CONFIG_CTSHELL_CMD_NAME_MAX_LEN    16
#define CONFIG_CTSHELL_LINE_BUF_SIZE       128
#define CONFIG_CTSHELL_MAX_ARGS            16
#define CONFIG_CTSHELL_HISTORY_SIZE        5
#define CONFIG_CTSHELL_VAR_MAX_COUNT       8
#define CONFIG_CTSHELL_VAR_NAME_LEN        16
#define CONFIG_CTSHELL_VAR_VAL_LEN         32
#define CONFIG_CTSHELL_FIFO_SIZE           128
#ifdef CONFIG_CTSHELL_USE_FS
#define CONFIG_CTSHELL_FS_PATH_MAX         256
#define CONFIG_CTSHELL_FS_NAME_MAX         64
#endif
#define CONFIG_CTSHELL_PROMPT              "ctsh>> "

#endif
