/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <setjmp.h>
#include "ctshell_config.h"

#define ctshell_error(fmt, ...)   ctshell_printf("Error: " fmt "\r\n", ##__VA_ARGS__)
#define CTSHELL_UNUSED_PARAM(x) ((void)(x))

/**
 * @brief Shell IO interface.
 */
typedef struct {
    void (*write)(const char *str, uint16_t len, void *priv);

    uint32_t (*get_tick)(void);
} ctshell_io_t;

/**
 * @brief Shell environment variable.
 */
typedef struct {
    char name[CONFIG_CTSHELL_VAR_NAME_LEN];
    char value[CONFIG_CTSHELL_VAR_VAL_LEN];
    int used;
} ctshell_var_t;

// picked from https://github.com/ravachol/kew/blob/main/src/ui/termbox2_input.h
/* ASCII key constants */
#define CTSHELL_KEY_CTRL_TILDE 0x00
#define CTSHELL_KEY_CTRL_2 0x00 // clash with `CTRL_TILDE`
#define CTSHELL_KEY_CTRL_A 0x01
#define CTSHELL_KEY_CTRL_B 0x02
#define CTSHELL_KEY_CTRL_C 0x03
#define CTSHELL_KEY_CTRL_D 0x04
#define CTSHELL_KEY_CTRL_E 0x05
#define CTSHELL_KEY_CTRL_F 0x06
#define CTSHELL_KEY_CTRL_G 0x07
#define CTSHELL_KEY_BACKSPACE 0x08
#define CTSHELL_KEY_CTRL_H 0x08 // clash with `CTRL_BACKSPACE`
#define CTSHELL_KEY_TAB 0x09
#define CTSHELL_KEY_CTRL_I 0x09 // clash with `TAB`
#define CTSHELL_KEY_CTRL_J 0x0a
#define CTSHELL_KEY_CTRL_K 0x0b
#define CTSHELL_KEY_CTRL_L 0x0c
#define CTSHELL_KEY_ENTER 0x0d
#define CTSHELL_KEY_LF 0x0a
#define CTSHELL_KEY_CTRL_M 0x0d // clash with `ENTER`
#define CTSHELL_KEY_CTRL_N 0x0e
#define CTSHELL_KEY_CTRL_O 0x0f
#define CTSHELL_KEY_CTRL_P 0x10
#define CTSHELL_KEY_CTRL_Q 0x11
#define CTSHELL_KEY_CTRL_R 0x12
#define CTSHELL_KEY_CTRL_S 0x13
#define CTSHELL_KEY_CTRL_T 0x14
#define CTSHELL_KEY_CTRL_U 0x15
#define CTSHELL_KEY_CTRL_V 0x16
#define CTSHELL_KEY_CTRL_W 0x17
#define CTSHELL_KEY_CTRL_X 0x18
#define CTSHELL_KEY_CTRL_Y 0x19
#define CTSHELL_KEY_CTRL_Z 0x1a
#define CTSHELL_KEY_ESC 0x1b
#define CTSHELL_KEY_CTRL_LSQ_BRACKET 0x1b // clash with 'ESC'
#define CTSHELL_KEY_CTRL_3 0x1b           // clash with 'ESC'
#define CTSHELL_KEY_CTRL_4 0x1c
#define CTSHELL_KEY_CTRL_BACKSLASH 0x1c // clash with 'CTRL_4'
#define CTSHELL_KEY_CTRL_5 0x1d
#define CTSHELL_KEY_CTRL_RSQ_BRACKET 0x1d // clash with 'CTRL_5'
#define CTSHELL_KEY_CTRL_6 0x1e
#define CTSHELL_KEY_CTRL_7 0x1f
#define CTSHELL_KEY_CTRL_SLASH 0x1f      // clash with 'CTRL_7'
#define CTSHELL_KEY_CTRL_UNDERSCORE 0x1f // clash with 'CTRL_7'
#define CTSHELL_KEY_SPACE 0x20
#define CTSHELL_KEY_BACKSPACE2 0x7f
#define CTSHELL_KEY_CTRL_8 0x7f // clash with 'BACKSPACE2'

typedef enum {
    CTSHELL_EVT_NONE = 0,
    CTSHELL_EVT_NORMAL_CHAR,
    CTSHELL_EVT_ENTER,
    CTSHELL_EVT_BACKSPACE,
    CTSHELL_EVT_TAB,
    CTSHELL_EVT_UP,
    CTSHELL_EVT_DOWN,
    CTSHELL_EVT_LEFT,
    CTSHELL_EVT_RIGHT,
    CTSHELL_EVT_CTRL_C
} ctshell_key_event_t;

typedef struct {
    uint8_t state;
    char input;
    uint8_t next_state;
    ctshell_key_event_t evt;
} ctshell_dfa_trans_t;

typedef enum {
    CTSHELL_DFA_ROOT = 0,
    CTSHELL_DFA_ESC,
    CTSHELL_DFA_CSI,
    CTSHELL_DFA_TILDE
} ctshell_dfa_state_t;

#ifdef CONFIG_CTSHELL_USE_FS
/* File Types */
typedef enum {
    CTSHELL_FS_TYPE_UNKNOWN = 0,
    CTSHELL_FS_TYPE_FILE,
    CTSHELL_FS_TYPE_DIR
} ctshell_file_type_t;

/* Directory Entry Info */
typedef struct {
    char name[CONFIG_CTSHELL_FS_NAME_MAX];
    uint32_t size;
    ctshell_file_type_t type;
} ctshell_dirent_t;

/* File System Driver Interface */
typedef struct {
    int (*open)(const char *path, int flags);
    int (*close)(int fd);
    int (*read)(int fd, void *buf, uint32_t count);
    int (*write)(int fd, const void *buf, uint32_t count);
    uint32_t (*size)(int fd);
    int (*opendir)(const char *path, void **dir_handle);
    int (*readdir)(void *dir_handle, ctshell_dirent_t *entry);
    int (*closedir)(void *dir_handle);
    int (*stat)(const char *path, ctshell_dirent_t *info);
    int (*unlink)(const char *path);
    int (*mkdir)(const char *path);
    int (*lseek)(int fd, long offset, int whence);
} ctshell_fs_drv_t;
#endif

/**
 * @brief Main shell context.
 */
typedef struct {
    ctshell_io_t io;
    void *priv;

    char fifo_buf[CONFIG_CTSHELL_FIFO_SIZE];
    volatile uint16_t fifo_head;
    volatile uint16_t fifo_tail;

    ctshell_var_t vars[CONFIG_CTSHELL_VAR_MAX_COUNT];
    char line_buf[CONFIG_CTSHELL_LINE_BUF_SIZE];
    uint16_t line_len;
    uint16_t cur_pos;

    char history[CONFIG_CTSHELL_HISTORY_SIZE][CONFIG_CTSHELL_LINE_BUF_SIZE];
    uint8_t history_count;
    uint8_t history_index;

    uint8_t dfa_state;
    volatile int sigint;
    jmp_buf jump_env;
    int is_executing;

#ifdef CONFIG_CTSHELL_USE_FS
    const ctshell_fs_drv_t *fs_drv;
    char cwd[CONFIG_CTSHELL_FS_PATH_MAX];
#endif
} ctshell_ctx_t;

typedef enum {
    CTSHELL_ATTR_NONE   = 0,
    CTSHELL_ATTR_MENU   = (1 << 0),
    CTSHELL_ATTR_HIDDEN = (1 << 1),
} ctshell_cmd_attr_t;

#ifdef CONFIG_CTSHELL_USE_FS
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#define CTSHELL_O_TRUNC  1
#define CTSHELL_O_APPEND 2
#endif

typedef int (*ctshell_cmd_func_t)(int argc, char *argv[]);

typedef struct ctshell_cmd_t {
    const char *name;
    const char *desc;
    ctshell_cmd_func_t func;
    uint32_t attrs;
    const struct ctshell_cmd_t *parent;
} ctshell_cmd_t;

#if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM)
#define CTSHELL_SECTION(x) __attribute__((section(x)))
#define CTSHELL_USED       __attribute__((used))
#define CTSHELL_ALIGN      __attribute__((aligned(sizeof(void*))))
#else
#error "Current compiler is not supported yet."
#endif

#define CTSHELL_EXPORT_CMD(_name, _func, _desc, _attr) \
    const ctshell_cmd_t __ctshell_cmd_##_name \
    CTSHELL_SECTION("ctshell_cmd_section") \
    CTSHELL_USED \
    CTSHELL_ALIGN = { \
        .name   = #_name, \
        .desc   = _desc, \
        .func   = _func, \
        .parent = NULL, \
        .attrs  = _attr \
    }

#define CTSHELL_EXPORT_SUBCMD(_parent, _name, _func, _desc) \
    extern const ctshell_cmd_t __ctshell_cmd_##_parent; \
    const ctshell_cmd_t __ctshell_cmd_##_parent##_##_name \
    CTSHELL_SECTION("ctshell_cmd_section") \
    CTSHELL_USED \
    CTSHELL_ALIGN = { \
        .name   = #_name, \
        .desc   = _desc, \
        .func   = _func, \
        .parent = &__ctshell_cmd_##_parent, \
        .attrs  = CTSHELL_ATTR_NONE \
    }

typedef enum {
    CTSHELL_ARG_BOOL,
    CTSHELL_ARG_INT,
    CTSHELL_ARG_STR,
    CTSHELL_ARG_VERB,
#ifdef CONFIG_CTSHELL_USE_DOUBLE
    CTSHELL_ARG_DOUBLE,
#endif
} ctshell_arg_type_t;

typedef struct {
    const char *flag;
    const char *key;
    ctshell_arg_type_t type;
    union {
        int i_val;
        char *s_val;
        int b_val;
#ifdef CONFIG_CTSHELL_USE_DOUBLE
        double d_val;
#endif
    } value;
    int found;
} ctshell_arg_def_t;

typedef struct {
    ctshell_arg_def_t args[CONFIG_CTSHELL_MAX_ARGS];
    int count;
    int argc;
    char **argv;
} ctshell_arg_parser_t;

void ctshell_init(ctshell_ctx_t *ctx, ctshell_io_t io, void *priv);
void ctshell_input(ctshell_ctx_t *ctx, char byte);
void ctshell_poll(ctshell_ctx_t *ctx);
void ctshell_printf(const char *fmt, ...);
void ctshell_check_abort(ctshell_ctx_t *ctx);
void ctshell_delay(ctshell_ctx_t *ctx, uint32_t ms);
void ctshell_args_init(ctshell_arg_parser_t *parser, int argc, char *argv[]);
void ctshell_expect_int(ctshell_arg_parser_t *p, const char *flag, const char *key);
void ctshell_expect_str(ctshell_arg_parser_t *p, const char *flag, const char *key);
void ctshell_expect_bool(ctshell_arg_parser_t *p, const char *flag, const char *key);
void ctshell_expect_verb(ctshell_arg_parser_t *p, const char *verb_name);
#ifdef CONFIG_CTSHELL_USE_DOUBLE
void ctshell_expect_double(ctshell_arg_parser_t *p, const char *flag, const char *key);
#endif
void ctshell_args_parse(ctshell_arg_parser_t *p);
int ctshell_get_int(ctshell_arg_parser_t *p, const char *key);
char *ctshell_get_str(ctshell_arg_parser_t *p, const char *key);
int ctshell_get_bool(ctshell_arg_parser_t *p, const char *key);
#ifdef CONFIG_CTSHELL_USE_DOUBLE
double ctshell_get_double(ctshell_arg_parser_t *p, const char *key);
#endif
int ctshell_has(ctshell_arg_parser_t *p, const char *key);
#ifdef CONFIG_CTSHELL_USE_FS
#ifdef CONFIG_CTSHELL_USE_FS_FATFS
extern void ctshell_fatfs_init(ctshell_ctx_t *ctx);
#endif
#endif

#ifdef __cplusplus
}
#endif
