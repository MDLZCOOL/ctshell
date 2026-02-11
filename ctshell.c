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

#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
extern const ctshell_cmd_t Image$$CtshellCmdSection$$Base;
extern const ctshell_cmd_t Image$$CtshellCmdSection$$Limit;
#define CMD_START (&Image$$CtshellCmdSection$$Base)
#define CMD_END   (&Image$$CtshellCmdSection$$Limit)
#elif defined(__GNUC__) || defined(__clang__)
extern const ctshell_cmd_t __start_ctshell_cmd_section;
extern const ctshell_cmd_t __stop_ctshell_cmd_section;
#define CMD_START (&__start_ctshell_cmd_section)
#define CMD_END   (&__stop_ctshell_cmd_section)
#endif

static ctshell_ctx_t *g_ctshell_ctx = NULL;

static const ctshell_dfa_trans_t dfa_table[] = {
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_ENTER,      CTSHELL_DFA_ROOT, CTSHELL_EVT_ENTER},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_LF,         CTSHELL_DFA_ROOT, CTSHELL_EVT_ENTER},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_BACKSPACE,  CTSHELL_DFA_ROOT, CTSHELL_EVT_BACKSPACE},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_BACKSPACE2, CTSHELL_DFA_ROOT, CTSHELL_EVT_BACKSPACE},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_TAB,        CTSHELL_DFA_ROOT, CTSHELL_EVT_TAB},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_CTRL_C,     CTSHELL_DFA_ROOT, CTSHELL_EVT_CTRL_C},
        {CTSHELL_DFA_ROOT, CTSHELL_KEY_ESC,        CTSHELL_DFA_ESC,  CTSHELL_EVT_NONE},
        {CTSHELL_DFA_ESC, '[',                     CTSHELL_DFA_CSI,  CTSHELL_EVT_NONE},
        {CTSHELL_DFA_CSI, 'A',                     CTSHELL_DFA_ROOT, CTSHELL_EVT_UP},
        {CTSHELL_DFA_CSI, 'B',                     CTSHELL_DFA_ROOT, CTSHELL_EVT_DOWN},
        {CTSHELL_DFA_CSI, 'C',                     CTSHELL_DFA_ROOT, CTSHELL_EVT_RIGHT},
        {CTSHELL_DFA_CSI, 'D',                     CTSHELL_DFA_ROOT, CTSHELL_EVT_LEFT},
};

#define DFA_TABLE_SIZE (sizeof(dfa_table) / sizeof(ctshell_dfa_trans_t))

static void ctshell_write(ctshell_ctx_t *ctx, const char *str, int len) {
    if (ctx && ctx->io.write && str && len > 0) {
        ctx->io.write(str, (uint16_t) len, ctx->priv);
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
        g_ctshell_ctx->io.write(buf, (uint16_t) len, g_ctshell_ctx->priv);
    }
}

static void ctshell_cursor_left(ctshell_ctx_t *ctx) { ctshell_puts(ctx, "\033[D"); }
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
    for (int i = 0; i < CONFIG_CTSHELL_VAR_MAX_COUNT; i++) {
        if (ctx->vars[i].used && strcmp(ctx->vars[i].name, name) == 0) {
            return &ctx->vars[i];
        }
    }
    return NULL;
}

static int set_var(ctshell_ctx_t *ctx, const char *name, const char *value) {
    ctshell_var_t *var = find_var(ctx, name);
    if (var) {
        strncpy(var->value, value, CONFIG_CTSHELL_VAR_VAL_LEN - 1);
        var->value[CONFIG_CTSHELL_VAR_VAL_LEN - 1] = '\0';
        return 0;
    }
    for (int i = 0; i < CONFIG_CTSHELL_VAR_MAX_COUNT; i++) {
        if (!ctx->vars[i].used) {
            strncpy(ctx->vars[i].name, name, CONFIG_CTSHELL_VAR_NAME_LEN - 1);
            ctx->vars[i].name[CONFIG_CTSHELL_VAR_NAME_LEN - 1] = '\0';
            strncpy(ctx->vars[i].value, value, CONFIG_CTSHELL_VAR_VAL_LEN - 1);
            ctx->vars[i].value[CONFIG_CTSHELL_VAR_VAL_LEN - 1] = '\0';
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
        char var_name[CONFIG_CTSHELL_VAR_NAME_LEN] = {0};
        char *end = p + 1;
        int n_len = 0;

        while (*end && (isalnum((int) *end) || *end == '_') && n_len < CONFIG_CTSHELL_VAR_NAME_LEN - 1) {
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

        if (strlen(ctx->line_buf) + diff >= CONFIG_CTSHELL_LINE_BUF_SIZE - 1) return 0;

        memmove(p + val_len, end, strlen(end) + 1);
        memcpy(p, val_str, val_len);
        ctx->line_len += diff;
        p = strchr(p + val_len, '$');
    }
    return 1;
}

static void ctshell_save_history(ctshell_ctx_t *ctx) {
    if (ctx->line_len == 0) return;
    int idx = ctx->history_count % CONFIG_CTSHELL_HISTORY_SIZE;
    strncpy(ctx->history[idx], ctx->line_buf, CONFIG_CTSHELL_LINE_BUF_SIZE - 1);
    ctx->history[idx][CONFIG_CTSHELL_LINE_BUF_SIZE - 1] = '\0';
    ctx->history_count++;
    ctx->history_index = ctx->history_count;
}

static void ctshell_load_history(ctshell_ctx_t *ctx, int index) {
    ctshell_clear_line_view(ctx);
    strncpy(ctx->line_buf, ctx->history[index], CONFIG_CTSHELL_LINE_BUF_SIZE - 1);
    ctx->line_len = strlen(ctx->line_buf);
    ctx->cur_pos = ctx->line_len;
    ctshell_puts(ctx, ctx->line_buf);
}

static const ctshell_cmd_t *find_cmd_in_section(const char *name, const ctshell_cmd_t *parent) {
    const ctshell_cmd_t *cmd = CMD_START;
    const ctshell_cmd_t *end = CMD_END;
    for (; cmd < end; cmd++) {
        if (cmd->parent == parent && strcmp(cmd->name, name) == 0) {
            return cmd;
        }
    }
    return NULL;
}

static int ctshell_has_children(const ctshell_cmd_t *parent) {
    const ctshell_cmd_t *cmd = CMD_START;
    const ctshell_cmd_t *end = CMD_END;
    for (; cmd < end; cmd++) {
        if (cmd->parent == parent) {
            return 1;
        }
    }
    return 0;
}

static int ctshell_is_menu(const ctshell_cmd_t *cmd) {
    if (!cmd) return 0;
    if (cmd->attrs & CTSHELL_ATTR_MENU) return 1;
    if (cmd->func == NULL) return 1;
    return ctshell_has_children(cmd);
}

void ctshell_check_abort(ctshell_ctx_t *ctx) {
    if (ctx && ctx->sigint) {
        ctx->sigint = 0;
        longjmp(ctx->jump_env, 1);
    }
}

static int is_trailing_space(const char *str) {
    if (!str || *str == '\0') return 0;
    int len = strlen(str);
    return str[len - 1] == ' ';
}

static void ctshell_tab_complete(ctshell_ctx_t *ctx) {
    if (ctx->line_len == 0) return;

    char buf_copy[CONFIG_CTSHELL_LINE_BUF_SIZE];
    strncpy(buf_copy, ctx->line_buf, sizeof(buf_copy));
    buf_copy[CONFIG_CTSHELL_LINE_BUF_SIZE - 1] = '\0';

    char *argv[CONFIG_CTSHELL_MAX_ARGS];
    int argc = 0;
    char *p = buf_copy;
    while (*p && argc < CONFIG_CTSHELL_MAX_ARGS) {
        while (*p == ' ') *p++ = '\0';
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ') p++;
    }

    const ctshell_cmd_t *parent_cmd = NULL;
    char *match_prefix = "";
    int match_len = 0;
    int has_space = is_trailing_space(ctx->line_buf);

    int parse_depth = has_space ? argc : (argc - 1);
    int valid_path = 1;
    for (int i = 0; i < parse_depth; i++) {
        const ctshell_cmd_t *found = find_cmd_in_section(argv[i], parent_cmd);
        if (found && ctshell_is_menu(found)) {
            parent_cmd = found;
        } else {
            valid_path = 0;
            break;
        }
    }
    if (!valid_path) return;
    if (!has_space && argc > 0) {
        match_prefix = argv[argc - 1];
    }
    match_len = strlen(match_prefix);
    const ctshell_cmd_t *cmd = CMD_START;
    const ctshell_cmd_t *end = CMD_END;
    int match_count = 0;
    const ctshell_cmd_t *last_match = NULL;
    for (; cmd < end; cmd++) {
        if (cmd->parent == parent_cmd &&
            strncmp(cmd->name, match_prefix, match_len) == 0 &&
            !(cmd->attrs & CTSHELL_ATTR_HIDDEN)) {
            last_match = cmd;
            match_count++;
        }
    }
    if (match_count == 1) {
        const char *full_name = last_match->name;
        int full_len = strlen(full_name);
        for (int i = match_len; i < full_len; i++) {
            if (ctx->line_len < CONFIG_CTSHELL_LINE_BUF_SIZE - 2) {
                char c = full_name[i];
                ctshell_write(ctx, &c, 1);
                ctx->line_buf[ctx->line_len++] = c;
                ctx->cur_pos++;
            }
        }
        if (ctx->line_len < CONFIG_CTSHELL_LINE_BUF_SIZE - 1) {
            ctshell_write(ctx, " ", 1);
            ctx->line_buf[ctx->line_len++] = ' ';
            ctx->cur_pos++;
        }
    } else if (match_count > 1) {
        ctshell_puts(ctx, "\r\n");
        for (cmd = CMD_START; cmd < end; cmd++) {
            if (cmd->parent == parent_cmd &&
                strncmp(cmd->name, match_prefix, match_len) == 0 &&
                !(cmd->attrs & CTSHELL_ATTR_HIDDEN)) {
                if (ctshell_is_menu(cmd)) {
                    ctshell_printf("%s/  ", cmd->name);
                } else {
                    ctshell_printf("%s   ", cmd->name);
                }
            }
        }
        ctshell_puts(ctx, "\r\n" CONFIG_CTSHELL_PROMPT);
        ctshell_puts(ctx, ctx->line_buf);
    }
}

static void ctshell_exec(ctshell_ctx_t *ctx) {
    ctx->sigint = 0;
    ctshell_save_history(ctx);
    if (ctx->line_len == 0) return;
    ctshell_expand_vars(ctx);
    char *argv[CONFIG_CTSHELL_MAX_ARGS];
    int argc = 0;
    char *p = ctx->line_buf;
    while (*p && argc < CONFIG_CTSHELL_MAX_ARGS) {
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
    const ctshell_cmd_t *cur_cmd = NULL;
    const ctshell_cmd_t *parent_cmd = NULL;
    int arg_idx = 0;
    while (arg_idx < argc) {
        const ctshell_cmd_t *found = find_cmd_in_section(argv[arg_idx], parent_cmd);
        if (found) {
            cur_cmd = found;
            if (ctshell_is_menu(cur_cmd) && (arg_idx + 1 < argc)) {
                const ctshell_cmd_t *child = find_cmd_in_section(argv[arg_idx + 1], cur_cmd);
                if (child) {
                    parent_cmd = cur_cmd;
                    arg_idx++;
                    continue;
                }
            }
            break;
        } else {
            cur_cmd = NULL;
            break;
        }
    }
    if (cur_cmd) {
        if (cur_cmd->func == NULL) {
            ctshell_printf("\r\nCommand group '%s'. Sub-commands:\r\n", cur_cmd->name);
            const ctshell_cmd_t *c = CMD_START;
            for (; c < CMD_END; c++) {
                if (c->parent == cur_cmd && !(c->attrs & CTSHELL_ATTR_HIDDEN)) {
                    ctshell_printf("  %-12s : %s\r\n", c->name, c->desc);
                }
            }
        } else {
            ctshell_puts(ctx, "\r\n");
            ctx->is_executing = 1;
            if (setjmp(ctx->jump_env) == 0) {
                cur_cmd->func(argc - arg_idx, &argv[arg_idx]);
            } else {
                ctshell_printf("\r\n^C\r\nCommand aborted.\r\n");
            }
            ctx->is_executing = 0;
        }
    } else {
        ctshell_printf("\r\n%s: command not found", argv[arg_idx]);
    }
}

static ctshell_key_event_t dfa_parse(ctshell_ctx_t *ctx, char byte) {
    for (int i = 0; i < (int) DFA_TABLE_SIZE; i++) {
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
    if (ctx->line_len < CONFIG_CTSHELL_LINE_BUF_SIZE - 1) {
        memmove(&ctx->line_buf[ctx->cur_pos + 1], &ctx->line_buf[ctx->cur_pos], ctx->line_len - ctx->cur_pos + 1);
        ctx->line_buf[ctx->cur_pos] = byte;
        ctx->line_len++;
        ctx->cur_pos++;
        ctshell_write(ctx, &byte, 1);
        ctshell_redraw_tail(ctx);
    }
}

static void hdl_enter(ctshell_ctx_t *ctx, char byte) {
    CTSHELL_UNUSED_PARAM(byte);

    ctshell_exec(ctx);
    ctx->line_len = 0;
    ctx->cur_pos = 0;
    memset(ctx->line_buf, 0, CONFIG_CTSHELL_LINE_BUF_SIZE);
    ctshell_puts(ctx, "\r\n" CONFIG_CTSHELL_PROMPT);
}

static void hdl_backspace(ctshell_ctx_t *ctx, char byte) {
    CTSHELL_UNUSED_PARAM(byte);

    if (ctx->cur_pos > 0) {
        memmove(&ctx->line_buf[ctx->cur_pos - 1], &ctx->line_buf[ctx->cur_pos], ctx->line_len - ctx->cur_pos + 1);
        ctx->cur_pos--;
        ctx->line_len--;
        ctshell_cursor_left(ctx);
        ctshell_redraw_tail(ctx);
    }
}

static void hdl_history_prev(ctshell_ctx_t *ctx, char byte) {
    CTSHELL_UNUSED_PARAM(byte);

    if (ctx->history_count > 0 && ctx->history_index > 0) {
        ctx->history_index--;
        ctshell_load_history(ctx, ctx->history_index % CONFIG_CTSHELL_HISTORY_SIZE);
    }
}

static void hdl_history_next(ctshell_ctx_t *ctx, char byte) {
    CTSHELL_UNUSED_PARAM(byte);

    if (ctx->history_index < ctx->history_count - 1) {
        ctx->history_index++;
        ctshell_load_history(ctx, ctx->history_index % CONFIG_CTSHELL_HISTORY_SIZE);
    } else {
        ctx->history_index = ctx->history_count;
        ctshell_clear_line_view(ctx);
    }
}

static void hdl_cursor_left(ctshell_ctx_t *ctx, char byte) {
    CTSHELL_UNUSED_PARAM(byte);

    if (ctx->cur_pos > 0) {
        ctx->cur_pos--;
        ctshell_cursor_left(ctx);
    }
}

static void hdl_cursor_right(ctshell_ctx_t *ctx, char byte) {
    CTSHELL_UNUSED_PARAM(byte);

    if (ctx->cur_pos < ctx->line_len) {
        ctx->cur_pos++;
        ctshell_cursor_right(ctx);
    }
}

static void hdl_ctrl_c(ctshell_ctx_t *ctx, char byte) {
    CTSHELL_UNUSED_PARAM(byte);

    ctshell_puts(ctx, "\r\n" CONFIG_CTSHELL_PROMPT);
    ctx->line_len = 0;
    ctx->cur_pos = 0;
    memset(ctx->line_buf, 0, CONFIG_CTSHELL_LINE_BUF_SIZE);
}

static void hdl_tab(ctshell_ctx_t *ctx, char byte) {
    CTSHELL_UNUSED_PARAM(byte);

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

    if (evt != CTSHELL_EVT_NONE && evt < (sizeof(action_map) / sizeof(action_map[0]))) {
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
            uint16_t next_head = (ctx->fifo_head + 1) % CONFIG_CTSHELL_FIFO_SIZE;
            if (next_head != ctx->fifo_tail) {
                ctx->fifo_buf[ctx->fifo_head] = byte;
                ctx->fifo_head = next_head;
            }
        }
        return;
    }

    uint16_t next_head = (ctx->fifo_head + 1) % CONFIG_CTSHELL_FIFO_SIZE;
    if (next_head != ctx->fifo_tail) {
        ctx->fifo_buf[ctx->fifo_head] = byte;
        ctx->fifo_head = next_head;
    }
}

void ctshell_poll(ctshell_ctx_t *ctx) {
    while (ctx->fifo_head != ctx->fifo_tail) {
        char byte = ctx->fifo_buf[ctx->fifo_tail];
        ctx->fifo_tail = (ctx->fifo_tail + 1) % CONFIG_CTSHELL_FIFO_SIZE;

        ctshell_handle_byte(ctx, byte);
    }
}

void ctshell_init(ctshell_ctx_t *ctx, ctshell_io_t io, void *priv) {
    memset(ctx, 0, sizeof(ctshell_ctx_t));
    ctx->io = io;
    ctx->priv = priv;
    g_ctshell_ctx = ctx;
    ctshell_puts(ctx, "\r\n" CONFIG_CTSHELL_PROMPT);
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
    if (p->count >= CONFIG_CTSHELL_MAX_ARGS) return;
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

#ifdef CONFIG_CTSHELL_USE_DOUBLE
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
                } else if (k + 1 < p->argc) {
                    if (def->type == CTSHELL_ARG_INT) {
                        def->value.i_val = (int) strtol(p->argv[k + 1], NULL, 0);
                    } else if (def->type == CTSHELL_ARG_STR) {
                        def->value.s_val = p->argv[k + 1];
                    }
#ifdef CONFIG_CTSHELL_USE_DOUBLE
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

static ctshell_arg_def_t *_find_res(ctshell_arg_parser_t *p, const char *key) {
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

char *ctshell_get_str(ctshell_arg_parser_t *p, const char *key) {
    ctshell_arg_def_t *d = _find_res(p, key);
    return (d && d->found) ? d->value.s_val : NULL;
}

int ctshell_get_bool(ctshell_arg_parser_t *p, const char *key) {
    ctshell_arg_def_t *d = _find_res(p, key);
    return (d && d->found) ? d->value.b_val : 0;
}

#ifdef CONFIG_CTSHELL_USE_DOUBLE
double ctshell_get_double(ctshell_arg_parser_t *p, const char *key) {
    ctshell_arg_def_t *d = _find_res(p, key);
    return (d && d->found) ? d->value.d_val : 0.0;
}
#endif

int ctshell_has(ctshell_arg_parser_t *p, const char *key) {
    ctshell_arg_def_t *d = _find_res(p, key);
    return (d && d->found);
}

#ifdef CONFIG_CTSHELL_USE_FS
#define CHECK_FS_READY() \
    if (!g_ctshell_ctx || !g_ctshell_ctx->fs_drv) { \
        ctshell_error("Filesystem not initialized.\r\n"); \
        return -1; \
    }

void ctshell_fs_resolve_path(const char *cwd, const char *path, char *out_buf, size_t buf_size) {
    if (!path || !out_buf || buf_size == 0) return;
    char temp[CONFIG_CTSHELL_FS_PATH_MAX];
    size_t len = 0;
    if (path[0] == '/') {
        temp[0] = '/';
        temp[1] = '\0';
        len = 1;
        path++;
    } else {
        strncpy(temp, cwd, sizeof(temp) - 1);
        len = strlen(temp);
        if (len > 0 && temp[len - 1] != '/') {
            temp[len++] = '/';
            temp[len] = '\0';
        }
    }
    const char *p = path;
    const char *token_end;
    while (*p) {
        token_end = p;
        while (*token_end && *token_end != '/') token_end++;
        size_t token_len = token_end - p;
        if (token_len == 0) {
        } else if (token_len == 1 && p[0] == '.') {
        } else if (token_len == 2 && p[0] == '.' && p[1] == '.') {
            if (len > 1) {
                len--;
                while (len > 1 && temp[len - 1] != '/') len--;
                temp[len] = '\0';
            }
        } else {
            if (len + token_len + 1 < sizeof(temp)) {
                memcpy(&temp[len], p, token_len);
                len += token_len;
                temp[len++] = '/';
                temp[len] = '\0';
            }
        }
        p = token_end;
        if (*p == '/') p++;
    }
    if (len > 1 && temp[len - 1] == '/') {
        temp[len - 1] = '\0';
    }
    strncpy(out_buf, temp, buf_size - 1);
    out_buf[buf_size - 1] = '\0';
}

void ctshell_fs_init(ctshell_ctx_t *ctx, const ctshell_fs_drv_t *drv) {
    if (ctx && drv) {
        ctx->fs_drv = drv;
        strcpy(ctx->cwd, "/");
    }
}
#endif

#ifdef CONFIG_CTSHELL_USE_BUILTIN_CMDS
static int cmd_help(int argc, char *argv[]) {
    const ctshell_cmd_t *target_parent = NULL;
    if (argc > 1) {
        target_parent = find_cmd_in_section(argv[1], NULL);
        if (!target_parent) {
            ctshell_printf("\r\n%s: command not found", argv[1]);
            return 0;
        }
    }
    ctshell_printf("Available commands:\r\n");
    const ctshell_cmd_t *cmd = CMD_START;
    const ctshell_cmd_t *end = CMD_END;
    for (; cmd < end; cmd++) {
        if (cmd->attrs & CTSHELL_ATTR_HIDDEN) continue;
        if (cmd->parent == target_parent) {
            char name_buf[CONFIG_CTSHELL_CMD_NAME_MAX_LEN];
            if (ctshell_is_menu(cmd)) {
                snprintf(name_buf, sizeof(name_buf), "%s/", cmd->name);
            } else {
                snprintf(name_buf, sizeof(name_buf), "%s", cmd->name);
            }
            ctshell_printf("  %-15s : %s\r\n", name_buf, cmd->desc);
        }
    }
    return 0;
}
CTSHELL_EXPORT_CMD(help, cmd_help, "Show help info", CTSHELL_ATTR_NONE);

static int cmd_clear(int argc, char *argv[]) {
    CTSHELL_UNUSED_PARAM(argv);
    if (argc != 1) {
        ctshell_printf("Usage: clear\r\n");
        return 0;
    }
    ctshell_printf("\033[2J\033[H\r\n");
    return 0;
}
CTSHELL_EXPORT_CMD(clear, cmd_clear, "Clear screen", CTSHELL_ATTR_NONE);

static int cmd_echo(int argc, char *argv[]) {
#ifndef CONFIG_CTSHELL_USE_FS
    for (int i = 1; i < argc; i++) {
        ctshell_printf("%s ", argv[i]);
    }
    ctshell_printf("\r\n");
    return 0;
#else
    int redirect_idx = -1;
    int append_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], ">") == 0) {
            redirect_idx = i;
            append_mode = 0;
            break;
        } else if (strcmp(argv[i], ">>") == 0) {
            redirect_idx = i;
            append_mode = 1;
            break;
        }
    }
    if (redirect_idx == -1) {
        for (int i = 1; i < argc; i++) {
            ctshell_printf("%s%s", argv[i], (i < argc - 1) ? " " : "");
        }
        ctshell_printf("\r\n");
        return 0;
    }
    CHECK_FS_READY();
    if (redirect_idx + 1 >= argc) {
        ctshell_printf("echo: syntax error near unexpected token 'newline'\r\n");
        return 0;
    }
    char path[CONFIG_CTSHELL_FS_PATH_MAX];
    const char *target = argv[redirect_idx + 1];
    ctshell_fs_resolve_path(g_ctshell_ctx->cwd, target, path, sizeof(path));
    int open_flag = append_mode ? CTSHELL_O_APPEND : CTSHELL_O_TRUNC;
    int fd = g_ctshell_ctx->fs_drv->open(path, open_flag);
    if (fd < 0) {
        ctshell_printf("echo: cannot open file '%s'\r\n", path);
        return 0;
    }
    if (append_mode && g_ctshell_ctx->fs_drv->lseek) {
        g_ctshell_ctx->fs_drv->lseek(fd, 0, SEEK_END);
    }
    for (int i = 1; i < redirect_idx; i++) {
        const char *str = argv[i];
        g_ctshell_ctx->fs_drv->write(fd, str, strlen(str));
        if (i < redirect_idx - 1) {
            g_ctshell_ctx->fs_drv->write(fd, " ", 1);
        }
    }
    g_ctshell_ctx->fs_drv->write(fd, "\r\n", 2);
    g_ctshell_ctx->fs_drv->close(fd);
    return 0;
#endif
}
CTSHELL_EXPORT_CMD(echo, cmd_echo, "Echo args to stdout or file", CTSHELL_ATTR_NONE);

static int cmd_set(int argc, char *argv[]) {
    if (!g_ctshell_ctx) return -1;
    if (argc == 1) {
        for (int i = 0; i < CONFIG_CTSHELL_VAR_MAX_COUNT; i++) {
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
CTSHELL_EXPORT_CMD(set, cmd_set, "Set or list variables", CTSHELL_ATTR_NONE);

static int cmd_unset(int argc, char *argv[]) {
    if (!g_ctshell_ctx || argc != 2) {
        ctshell_printf("Usage: unset <NAME>\r\n");
        return 0;
    }
    unset_var(g_ctshell_ctx, argv[1]);
    return 0;
}
CTSHELL_EXPORT_CMD(unset, cmd_unset, "Unset a variable", CTSHELL_ATTR_NONE);

#ifdef CONFIG_CTSHELL_USE_FS
static int cmd_ls(int argc, char *argv[]) {
    CHECK_FS_READY();
    char path[CONFIG_CTSHELL_FS_PATH_MAX];
    const char *target = (argc > 1) ? argv[1] : ".";
    ctshell_fs_resolve_path(g_ctshell_ctx->cwd, target, path, sizeof(path));
    void *dir;
    if (g_ctshell_ctx->fs_drv->opendir(path, &dir) != 0) {
        ctshell_printf("ls: cannot access '%s': No such directory\r\n", path);
        return 0;
    }
    ctshell_printf("Directory %s:\r\n", path);
    ctshell_printf("Type  Size        Name\r\n");
    ctshell_printf("----  ----------  ----\r\n");
    ctshell_dirent_t entry;
    while (g_ctshell_ctx->fs_drv->readdir(dir, &entry) == 0) {
        ctshell_printf("%-4s  %10u  %s\r\n",
                       (entry.type == CTSHELL_FS_TYPE_DIR) ? "DIR" : "FILE",
                       entry.size,
                       entry.name);
    }
    g_ctshell_ctx->fs_drv->closedir(dir);
    return 0;
}
CTSHELL_EXPORT_CMD(ls, cmd_ls, "List directory content", CTSHELL_ATTR_NONE);

static int cmd_cd(int argc, char *argv[]) {
    CHECK_FS_READY();
    if (argc > 2) {
        ctshell_printf("Usage: cd <path>\r\n");
        return 0;
    }
    const char *target = (argc == 2) ? argv[1] : "/";
    char path[CONFIG_CTSHELL_FS_PATH_MAX];
    ctshell_fs_resolve_path(g_ctshell_ctx->cwd, target, path, sizeof(path));
    ctshell_dirent_t info;
    if (g_ctshell_ctx->fs_drv->stat(path, &info) == 0) {
        if (info.type == CTSHELL_FS_TYPE_DIR) {
            strncpy(g_ctshell_ctx->cwd, path, CONFIG_CTSHELL_FS_PATH_MAX - 1);
        } else {
            ctshell_printf("cd: '%s': Not a directory\r\n", path);
        }
    } else {
        ctshell_printf("cd: '%s': No such file or directory\r\n", path);
    }
    return 0;
}
CTSHELL_EXPORT_CMD(cd, cmd_cd, "Change current directory", CTSHELL_ATTR_NONE);

static int cmd_pwd(int argc, char *argv[]) {
    CHECK_FS_READY();
    ctshell_printf("%s\r\n", g_ctshell_ctx->cwd);
    return 0;
}
CTSHELL_EXPORT_CMD(pwd, cmd_pwd, "Print working directory", CTSHELL_ATTR_NONE);

static int cmd_cat(int argc, char *argv[]) {
    CHECK_FS_READY();
    if (argc != 2) {
        ctshell_printf("Usage: cat <file>\r\n");
        return 0;
    }
    char path[CONFIG_CTSHELL_FS_PATH_MAX];
    ctshell_fs_resolve_path(g_ctshell_ctx->cwd, argv[1], path, sizeof(path));
    int fd = g_ctshell_ctx->fs_drv->open(path, 0);
    if (fd < 0) {
        ctshell_printf("cat: '%s': Cannot open file\r\n", path);
        return 0;
    }
    char buf[128];
    int bytes;
    while ((bytes = g_ctshell_ctx->fs_drv->read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        ctshell_printf("%s", buf);
    }
    ctshell_printf("\r\n");
    g_ctshell_ctx->fs_drv->close(fd);
    return 0;
}
CTSHELL_EXPORT_CMD(cat, cmd_cat, "Concatenate and print files", CTSHELL_ATTR_NONE);

static int cmd_mkdir(int argc, char *argv[]) {
    CHECK_FS_READY();
    if (argc != 2) {
        ctshell_printf("Usage: mkdir <path>\r\n");
        return 0;
    }
    char path[CONFIG_CTSHELL_FS_PATH_MAX];
    ctshell_fs_resolve_path(g_ctshell_ctx->cwd, argv[1], path, sizeof(path));

    if (g_ctshell_ctx->fs_drv->mkdir(path) != 0) {
        ctshell_printf("mkdir: cannot create directory '%s'\r\n", path);
    }
    return 0;
}
CTSHELL_EXPORT_CMD(mkdir, cmd_mkdir, "Create directory", CTSHELL_ATTR_NONE);

static int cmd_rm(int argc, char *argv[]) {
    CHECK_FS_READY();
    if (argc != 2) {
        ctshell_printf("Usage: rm <path>\r\n");
        return 0;
    }
    char path[CONFIG_CTSHELL_FS_PATH_MAX];
    ctshell_fs_resolve_path(g_ctshell_ctx->cwd, argv[1], path, sizeof(path));

    if (g_ctshell_ctx->fs_drv->unlink(path) != 0) {
        ctshell_printf("rm: cannot remove '%s'\r\n", path);
    }
    return 0;
}
CTSHELL_EXPORT_CMD(rm, cmd_rm, "Remove file or directory", CTSHELL_ATTR_NONE);

static int cmd_touch(int argc, char *argv[]) {
    CHECK_FS_READY();
    if (argc != 2) {
        ctshell_printf("Usage: touch <file>\r\n");
        return 0;
    }
    char path[CONFIG_CTSHELL_FS_PATH_MAX];
    ctshell_fs_resolve_path(g_ctshell_ctx->cwd, argv[1], path, sizeof(path));

    int fd = g_ctshell_ctx->fs_drv->open(path, 1);
    if (fd >= 0) {
        g_ctshell_ctx->fs_drv->close(fd);
    } else {
        ctshell_printf("touch: cannot create '%s'\r\n", path);
    }
    return 0;
}
CTSHELL_EXPORT_CMD(touch, cmd_touch, "Create empty file", CTSHELL_ATTR_NONE);
#endif
#endif
