/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell.h"
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <setjmp.h>

/* Linker symbols for command section */
extern const ctshell_cmd_t __start_ctshell_cmd_section;
extern const ctshell_cmd_t __stop_ctshell_cmd_section;

static ctshell_ctx_t *g_ctshell_ctx = NULL;

static const ctshell_dfa_trans_t dfa_table[] = {
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_ENTER,      CTSHELL_DFA_ROOT, CTSHELL_EVT_ENTER},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_LF,         CTSHELL_DFA_ROOT, CTSHELL_EVT_ENTER},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_BACKSPACE,  CTSHELL_DFA_ROOT, CTSHELL_EVT_BACKSPACE},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_BACKSPACE2, CTSHELL_DFA_ROOT, CTSHELL_EVT_BACKSPACE},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_TAB,        CTSHELL_DFA_ROOT, CTSHELL_EVT_TAB},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_CTRL_C,     CTSHELL_DFA_ROOT, CTSHELL_EVT_CTRL_C},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_ESC,        CTSHELL_DFA_ESC,  CTSHELL_EVT_NONE},
        {CTSHELL_DFA_ESC, '[',                CTSHELL_DFA_CSI,  CTSHELL_EVT_NONE},
        {CTSHELL_DFA_CSI, 'A',                CTSHELL_DFA_ROOT, CTSHELL_EVT_UP},
        {CTSHELL_DFA_CSI, 'B',                CTSHELL_DFA_ROOT, CTSHELL_EVT_DOWN},
        {CTSHELL_DFA_CSI, 'C',                CTSHELL_DFA_ROOT, CTSHELL_EVT_RIGHT},
        {CTSHELL_DFA_CSI, 'D',                CTSHELL_DFA_ROOT, CTSHELL_EVT_LEFT},
};

#define DFA_TABLE_SIZE (sizeof(dfa_table) / sizeof(ctshell_dfa_trans_t))

static const ctshell_cmd_t *get_cmd_start(void) { return &__start_ctshell_cmd_section; }
static const ctshell_cmd_t *get_cmd_end(void)   { return &__stop_ctshell_cmd_section; }

static void ctshell_write(ctshell_ctx_t *ctx, const char *str, int len) {
    if (ctx && ctx->io.write && str && len > 0) {
        ctx->io.write(str, (uint16_t)len, ctx->priv);
    }
}

static void ctshell_puts(ctshell_ctx_t *ctx, const char *str) {
    if (str) ctshell_write(ctx, str, strlen(str));
}

void ctshell_printf(const char *fmt, ...) {
    if (!g_ctshell_ctx || !g_ctshell_ctx->io.write) return;

    char buf[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        g_ctshell_ctx->io.write(buf, (uint16_t)len, g_ctshell_ctx->priv);
    }
}

static void ctshell_cursor_left(ctshell_ctx_t *ctx)  { ctshell_puts(ctx, "\033[D"); }
static void ctshell_cursor_right(ctshell_ctx_t *ctx) { ctshell_puts(ctx, "\033[C"); }

static void ctshell_redraw_tail(ctshell_ctx_t *ctx) {
    ctshell_puts(ctx, ctx->line_buf + ctx->cur_pos);
    ctshell_puts(ctx, " ");

    int tail_len = ctx->line_len - ctx->cur_pos + 1;
    while (tail_len--) {
        ctshell_cursor_left(ctx);
    }
}

static void ctshell_clear_line_view(ctshell_ctx_t *ctx) {
    while (ctx->cur_pos < ctx->line_len) {
        ctshell_puts(ctx, "\033[C");
        ctx->cur_pos++;
    }
    while (ctx->line_len > 0) {
        ctshell_puts(ctx, "\b \b");
        ctx->line_len--;
    }
    ctx->cur_pos = 0;
    ctx->line_buf[0] = '\0';
}

static ctshell_var_t *find_var(ctshell_ctx_t *ctx, const char *name) {
    for (int i = 0; i < CTSHELL_VAR_MAX_COUNT; i++) {
        if (ctx->vars[i].used && strcmp(ctx->vars[i].name, name) == 0) {
            return &ctx->vars[i];
        }
    }
    return NULL;
}

static int set_var(ctshell_ctx_t *ctx, const char *name, const char *value) {
    ctshell_var_t *var = find_var(ctx, name);
    if (var) {
        strncpy(var->value, value, CTSHELL_VAR_VAL_LEN - 1);
        var->value[CTSHELL_VAR_VAL_LEN - 1] = '\0';
        return 0;
    }
    for (int i = 0; i < CTSHELL_VAR_MAX_COUNT; i++) {
        if (!ctx->vars[i].used) {
            strncpy(ctx->vars[i].name, name, CTSHELL_VAR_NAME_LEN - 1);
            ctx->vars[i].name[CTSHELL_VAR_NAME_LEN - 1] = '\0';
            strncpy(ctx->vars[i].value, value, CTSHELL_VAR_VAL_LEN - 1);
            ctx->vars[i].value[CTSHELL_VAR_VAL_LEN - 1] = '\0';
            ctx->vars[i].used = 1;
            return 0;
        }
    }
    return -1;
}

static void unset_var(ctshell_ctx_t *ctx, const char *name) {
    ctshell_var_t *var = find_var(ctx, name);
    if (var) var->used = 0;
}

static int ctshell_expand_vars(ctshell_ctx_t *ctx) {
    char *p = strchr(ctx->line_buf, '$');
    if (!p) return 0;

    while (p) {
        char var_name[CTSHELL_VAR_NAME_LEN] = {0};
        char *end = p + 1;
        int n_len = 0;

        while (*end && (isalnum((int)*end) || *end == '_') && n_len < CTSHELL_VAR_NAME_LEN - 1) {
            var_name[n_len++] = *end++;
        }
        var_name[n_len] = '\0';

        if (n_len == 0) {
            p = strchr(p + 1, '$');
            continue;
        }

        ctshell_var_t *var = find_var(ctx, var_name);
        const char *val_str = var ? var->value : "";
        int val_len = strlen(val_str);
        int diff = val_len - (1 + n_len);

        if (strlen(ctx->line_buf) + diff >= CTSHELL_LINE_BUF_SIZE - 1) return 0;

        memmove(p + val_len, end, strlen(end) + 1);
        memcpy(p, val_str, val_len);
        ctx->line_len += diff;
        p = strchr(p + val_len, '$');
    }
    return 1;
}

static void ctshell_save_history(ctshell_ctx_t *ctx) {
    if (ctx->line_len == 0) return;
    int idx = ctx->history_count % CTSHELL_HISTORY_SIZE;
    strncpy(ctx->history[idx], ctx->line_buf, CTSHELL_LINE_BUF_SIZE - 1);
    ctx->history[idx][CTSHELL_LINE_BUF_SIZE - 1] = '\0';
    ctx->history_count++;
    ctx->history_index = ctx->history_count;
}

static void ctshell_load_history(ctshell_ctx_t *ctx, int index) {
    ctshell_clear_line_view(ctx);
    strncpy(ctx->line_buf, ctx->history[index], CTSHELL_LINE_BUF_SIZE - 1);
    ctx->line_len = strlen(ctx->line_buf);
    ctx->cur_pos = ctx->line_len;
    ctshell_puts(ctx, ctx->line_buf);
}

void ctshell_check_abort(ctshell_ctx_t *ctx) {
    if (ctx && ctx->sigint) {
        ctx->sigint = 0;
        longjmp(ctx->jump_env, 1);
    }
}

static int is_first_token(const char *buf, int len) {
    for (int i = 0; i < len; i++) if (buf[i] == ' ') return 0;
    return 1;
}

static void ctshell_tab_complete(ctshell_ctx_t *ctx) {
    if (ctx->line_len == 0 || !is_first_token(ctx->line_buf, ctx->line_len)) return;

    const ctshell_cmd_t *cmd = get_cmd_start();
    const ctshell_cmd_t *end = get_cmd_end();
    int match_count = 0;
    const ctshell_cmd_t *match = NULL;

    for (; cmd < end; cmd++) {
        if (strncmp(cmd->name, ctx->line_buf, ctx->line_len) == 0) {
            match = cmd;
            match_count++;
        }
    }

    if (match_count == 0) return;

    if (match_count == 1) {
        int name_len = strlen(match->name);
        for (int i = ctx->line_len; i < name_len; i++) {
            if (ctx->line_len < CTSHELL_LINE_BUF_SIZE - 1) {
                ctx->line_buf[ctx->line_len++] = match->name[i];
                ctshell_write(ctx, &match->name[i], 1);
            }
        }
        ctx->line_buf[ctx->line_len] = '\0';
        ctx->cur_pos = ctx->line_len;
    }
    else {
        ctshell_puts(ctx, "\r\n");
        for (cmd = get_cmd_start(); cmd < end; cmd++) {
            if (strncmp(cmd->name, ctx->line_buf, ctx->line_len) == 0) {
                ctshell_printf("%s  ", cmd->name);
            }
        }
        ctshell_puts(ctx, "\r\n" CTSHELL_PROMPT);
        ctshell_puts(ctx, ctx->line_buf);
    }
}

static void ctshell_exec(ctshell_ctx_t *ctx) {
    ctx->sigint = 0;
    ctshell_save_history(ctx);
    if (ctx->line_len == 0) return;

    ctshell_expand_vars(ctx);

    char *argv[CTSHELL_MAX_ARGS];
    int argc = 0;
    char *p = ctx->line_buf;

    while (*p && argc < CTSHELL_MAX_ARGS) {
        while (*p == ' ') *p++ = '\0';
        if (*p == '\0') break;

        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p != '\0' && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p != '\0' && *p != ' ') p++;
        }
    }

    if (argc == 0) return;

    const ctshell_cmd_t *cmd = get_cmd_start();
    const ctshell_cmd_t *end = get_cmd_end();
    int found = 0;

    for (; cmd < end; cmd++) {
        if (strcmp(argv[0], cmd->name) == 0) {
            found = 1;
            ctshell_puts(ctx, "\r\n");

            ctx->is_executing = 1;
            ctx->sigint = 0;

            if (setjmp(ctx->jump_env) == 0) {
                cmd->func(argc, argv);
            } else {
                ctshell_printf("\r\n^C\r\nCommand aborted.\r\n");
            }

            ctx->is_executing = 0;
            ctx->sigint = 0;
            break;
        }
    }

    if (!found) {
        ctshell_printf("\r\n%s: command not found", argv[0]);
    }
}

static ctshell_key_event_t dfa_parse(ctshell_ctx_t *ctx, char byte) {
    for (int i = 0; i < DFA_TABLE_SIZE; i++) {
        if (dfa_table[i].state == ctx->dfa_state && dfa_table[i].input == byte) {
            ctx->dfa_state = dfa_table[i].next_state;
            return dfa_table[i].evt;
        }
    }

    if (ctx->dfa_state != CTSHELL_DFA_ROOT) {
        ctx->dfa_state = CTSHELL_DFA_ROOT;
        return dfa_parse(ctx, byte);
    }

    if (byte >= 32 && byte <= 126) {
        return CTSHELL_EVT_NORMAL_CHAR;
    }

    return CTSHELL_EVT_NONE;
}

static void hdl_normal_char(ctshell_ctx_t *ctx, char byte) {
    if (ctx->line_len < CTSHELL_LINE_BUF_SIZE - 1) {
        memmove(&ctx->line_buf[ctx->cur_pos + 1], &ctx->line_buf[ctx->cur_pos], ctx->line_len - ctx->cur_pos + 1);
        ctx->line_buf[ctx->cur_pos] = byte;
        ctx->line_len++;
        ctx->cur_pos++;
        ctshell_write(ctx, &byte, 1);
        ctshell_redraw_tail(ctx);
    }
}

static void hdl_enter(ctshell_ctx_t *ctx, char byte) {
    ctshell_exec(ctx);
    ctx->line_len = 0;
    ctx->cur_pos = 0;
    memset(ctx->line_buf, 0, CTSHELL_LINE_BUF_SIZE);
    ctshell_puts(ctx, "\r\n" CTSHELL_PROMPT);
}

static void hdl_backspace(ctshell_ctx_t *ctx, char byte) {
    if (ctx->cur_pos > 0) {
        memmove(&ctx->line_buf[ctx->cur_pos - 1], &ctx->line_buf[ctx->cur_pos], ctx->line_len - ctx->cur_pos + 1);
        ctx->cur_pos--;
        ctx->line_len--;
        ctshell_cursor_left(ctx);
        ctshell_redraw_tail(ctx);
    }
}

static void hdl_history_prev(ctshell_ctx_t *ctx, char byte) {
    if (ctx->history_count > 0 && ctx->history_index > 0) {
        ctx->history_index--;
        ctshell_load_history(ctx, ctx->history_index % CTSHELL_HISTORY_SIZE);
    }
}

static void hdl_history_next(ctshell_ctx_t *ctx, char byte) {
    if (ctx->history_index < ctx->history_count - 1) {
        ctx->history_index++;
        ctshell_load_history(ctx, ctx->history_index % CTSHELL_HISTORY_SIZE);
    } else {
        ctx->history_index = ctx->history_count;
        ctshell_clear_line_view(ctx);
    }
}

static void hdl_cursor_left(ctshell_ctx_t *ctx, char byte) {
    if (ctx->cur_pos > 0) {
        ctx->cur_pos--;
        ctshell_cursor_left(ctx);
    }
}

static void hdl_cursor_right(ctshell_ctx_t *ctx, char byte) {
    if (ctx->cur_pos < ctx->line_len) {
        ctx->cur_pos++;
        ctshell_cursor_right(ctx);
    }
}

static void hdl_ctrl_c(ctshell_ctx_t *ctx, char byte) {
    ctshell_puts(ctx, "\r\n" CTSHELL_PROMPT);
    ctx->line_len = 0;
    ctx->cur_pos = 0;
    memset(ctx->line_buf, 0, CTSHELL_LINE_BUF_SIZE);
}

static void hdl_tab(ctshell_ctx_t *ctx, char byte) {
    ctshell_tab_complete(ctx);
}

typedef void (*event_handler_t)(ctshell_ctx_t *ctx, char byte);

static const event_handler_t action_map[] = {
        [CTSHELL_EVT_NONE]        = NULL,
        [CTSHELL_EVT_NORMAL_CHAR] = hdl_normal_char,
        [CTSHELL_EVT_ENTER]       = hdl_enter,
        [CTSHELL_EVT_BACKSPACE]   = hdl_backspace,
        [CTSHELL_EVT_TAB]         = hdl_tab,
        [CTSHELL_EVT_UP]          = hdl_history_prev,
        [CTSHELL_EVT_DOWN]        = hdl_history_next,
        [CTSHELL_EVT_LEFT]        = hdl_cursor_left,
        [CTSHELL_EVT_RIGHT]       = hdl_cursor_right,
        [CTSHELL_EVT_CTRL_C]      = hdl_ctrl_c,
};

static void ctshell_handle_byte(ctshell_ctx_t *ctx, char byte) {
    ctshell_key_event_t evt = dfa_parse(ctx, byte);

    if (evt != CTSHELL_EVT_NONE && evt < (sizeof(action_map)/sizeof(action_map[0]))) {
        event_handler_t handler = action_map[evt];
        if (handler) {
            handler(ctx, byte);
        }
    }
}

void ctshell_input(ctshell_ctx_t *ctx, char byte) {
    if (byte == CTSHELL_KEY_CTRL_C) {
        ctx->sigint = 1;
        if (!ctx->is_executing) {
            uint16_t next_head = (ctx->fifo_head + 1) % CTSHELL_FIFO_SIZE;
            if (next_head != ctx->fifo_tail) {
                ctx->fifo_buf[ctx->fifo_head] = byte;
                ctx->fifo_head = next_head;
            }
        }
        return;
    }

    uint16_t next_head = (ctx->fifo_head + 1) % CTSHELL_FIFO_SIZE;
    if (next_head != ctx->fifo_tail) {
        ctx->fifo_buf[ctx->fifo_head] = byte;
        ctx->fifo_head = next_head;
    }
}

void ctshell_poll(ctshell_ctx_t *ctx) {
    while (ctx->fifo_head != ctx->fifo_tail) {
        char byte = ctx->fifo_buf[ctx->fifo_tail];
        ctx->fifo_tail = (ctx->fifo_tail + 1) % CTSHELL_FIFO_SIZE;

        ctshell_handle_byte(ctx, byte);
    }
}

void ctshell_init(ctshell_ctx_t *ctx, ctshell_io_t io, void *priv) {
    memset(ctx, 0, sizeof(ctshell_ctx_t));
    ctx->io = io;
    ctx->priv = priv;
    g_ctshell_ctx = ctx;
    ctshell_puts(ctx, "\r\n" CTSHELL_PROMPT);
}

void ctshell_delay(ctshell_ctx_t *ctx, uint32_t ms) {
    if (!ctx || !ctx->io.get_tick || ms == 0) {
        return;
    }

    uint32_t start_tick = ctx->io.get_tick();

    while ((ctx->io.get_tick() - start_tick) < ms) {
        ctshell_check_abort(ctx);
    }
}

void ctshell_args_init(ctshell_arg_parser_t *p, int argc, char *argv[]) {
    memset(p, 0, sizeof(ctshell_arg_parser_t));
    p->argc = argc;
    p->argv = argv;
}

static void _add_arg(ctshell_arg_parser_t *p, const char *flag, const char *key, ctshell_arg_type_t type) {
    if (p->count >= CTSHELL_MAX_ARGS) return;
    p->args[p->count].flag = flag;
    p->args[p->count].key = key ? key : flag;
    p->args[p->count].type = type;
    p->count++;
}

void ctshell_expect_int(ctshell_arg_parser_t *p, const char *flag, const char *key) {
    _add_arg(p, flag, key, CTSHELL_ARG_INT);
}
void ctshell_expect_str(ctshell_arg_parser_t *p, const char *flag, const char *key) {
    _add_arg(p, flag, key, CTSHELL_ARG_STR);
}
void ctshell_expect_bool(ctshell_arg_parser_t *p, const char *flag, const char *key) {
    _add_arg(p, flag, key, CTSHELL_ARG_BOOL);
}
void ctshell_expect_verb(ctshell_arg_parser_t *p, const char *verb_name) {
    _add_arg(p, verb_name, verb_name, CTSHELL_ARG_VERB);
}
#ifdef CTSHELL_USE_DOUBLE
void ctshell_expect_double(ctshell_arg_parser_t *p, const char *flag, const char *key) {
    _add_arg(p, flag, key, CTSHELL_ARG_DOUBLE);
}
#endif

void ctshell_args_parse(ctshell_arg_parser_t *p) {
    for (int i = 0; i < p->count; i++) {
        ctshell_arg_def_t *def = &p->args[i];

        if (def->type == CTSHELL_ARG_VERB) {
            if (p->argc > 1 && strcmp(p->argv[1], def->flag) == 0) {
                def->found = 1;
            }
            continue;
        }

        for (int k = 1; k < p->argc; k++) {
            if (strcmp(p->argv[k], def->flag) == 0) {
                def->found = 1;

                if (def->type == CTSHELL_ARG_BOOL) {
                    def->value.b_val = 1;
                }
                else if (k + 1 < p->argc) {
                    if (def->type == CTSHELL_ARG_INT) {
                        def->value.i_val = (int)strtol(p->argv[k+1], NULL, 0);
                    }
                    else if (def->type == CTSHELL_ARG_STR) {
                        def->value.s_val = p->argv[k+1];
                    }
#ifdef CTSHELL_USE_DOUBLE
                    else if (def->type == CTSHELL_ARG_DOUBLE) {
                        def->value.d_val = strtod(p->argv[k+1], NULL);
                    }
#endif
                }
                break;
            }
        }
    }
}

static ctshell_arg_def_t* _find_res(ctshell_arg_parser_t *p, const char *key) {
    for (int i = 0; i < p->count; i++) {
        const char *target = p->args[i].key ? p->args[i].key : p->args[i].flag;
        if (strcmp(target, key) == 0) return &p->args[i];
    }
    return NULL;
}

int ctshell_get_int(ctshell_arg_parser_t *p, const char *key) {
    ctshell_arg_def_t *d = _find_res(p, key);
    return (d && d->found) ? d->value.i_val : 0;
}

char* ctshell_get_str(ctshell_arg_parser_t *p, const char *key) {
    ctshell_arg_def_t *d = _find_res(p, key);
    return (d && d->found) ? d->value.s_val : NULL;
}

int ctshell_get_bool(ctshell_arg_parser_t *p, const char *key) {
    ctshell_arg_def_t *d = _find_res(p, key);
    return (d && d->found) ? d->value.b_val : 0;
}

#ifdef CTSHELL_USE_DOUBLE
double ctshell_get_double(ctshell_arg_parser_t *p, const char *key) {
    ctshell_arg_def_t *d = _find_res(p, key);
    return (d && d->found) ? d->value.d_val : 0.0;
}
#endif

int ctshell_has(ctshell_arg_parser_t *p, const char *key) {
    ctshell_arg_def_t *d = _find_res(p, key);
    return (d && d->found);
}

static int cmd_help(int argc, char *argv[]) {
    if (argc != 1) {
        ctshell_printf("Usage: help\r\n");
        return 0;
    }
    const ctshell_cmd_t *cmd = get_cmd_start();
    const ctshell_cmd_t *end = get_cmd_end();
    ctshell_printf("Available commands:\r\n");
    for (; cmd < end; cmd++) {
        ctshell_printf("  %-10s : %s\r\n", cmd->name, cmd->desc);
    }
    return 0;
}
CTSHELL_EXPORT_CMD(help, cmd_help, "Show help info");

static int cmd_clear(int argc, char *argv[]) {
    if (argc != 1) {
        ctshell_printf("Usage: clear\r\n");
        return 0;
    }
    ctshell_printf("\033[2J\033[H\r\n");
    return 0;
}
CTSHELL_EXPORT_CMD(clear, cmd_clear, "Clear screen");

int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        ctshell_printf("%s ", argv[i]);
    }
    ctshell_printf("\r\n");
    return 0;
}
CTSHELL_EXPORT_CMD(echo, cmd_echo, "Echo arguments");

static int cmd_set(int argc, char *argv[]) {
    if (!g_ctshell_ctx) return -1;
    if (argc == 1) {
        for (int i = 0; i < CTSHELL_VAR_MAX_COUNT; i++) {
            if (g_ctshell_ctx->vars[i].used) {
                ctshell_printf("%s=%s\r\n", g_ctshell_ctx->vars[i].name, g_ctshell_ctx->vars[i].value);
            }
        }
        return 0;
    }
    if (argc == 3) {
        if (set_var(g_ctshell_ctx, argv[1], argv[2]) == 0) {
            ctshell_printf("Variable %s set to %s\r\n", argv[1], argv[2]);
        } else {
            ctshell_error("Variable list full\r\n");
        }
        return 0;
    }
    ctshell_printf("Usage: set [NAME] [VALUE] or set (list all)\r\n");
    return 0;
}
CTSHELL_EXPORT_CMD(set, cmd_set, "Set or list variables");

static int cmd_unset(int argc, char *argv[]) {
    if (!g_ctshell_ctx || argc != 2) {
        ctshell_printf("Usage: unset <NAME>\r\n");
        return 0;
    }
    unset_var(g_ctshell_ctx, argv[1]);
    return 0;
}
CTSHELL_EXPORT_CMD(unset, cmd_unset, "Unset a variable");
