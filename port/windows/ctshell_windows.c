/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell_windows.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    HANDLE hStdin;
    HANDLE hStdout;
    DWORD  dwOldMode;
} ctshell_windows_priv_t;

static ctshell_ctx_t *g_ctx;
static ctshell_windows_priv_t priv;

static void inject_ansi(ctshell_ctx_t *ctx, const char *ansi_seq) {
    while (*ansi_seq) {
        ctshell_input(ctx, *ansi_seq++);
    }
}

static void windows_shell_write(const char *str, uint16_t len, void *p) {
    UNREFERENCED_PARAMETER(p);
    DWORD dwWritten;
    WriteConsole(priv.hStdout, str, len, &dwWritten, NULL);
}

static uint32_t windows_get_tick(void) {
    return GetTickCount();
}

int ctshell_port_windows_init(ctshell_ctx_t *ctx) {
    if (ctx == NULL) {
        return -1;
    }
    g_ctx = ctx;
    priv.hStdin = GetStdHandle(STD_INPUT_HANDLE);
    priv.hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (priv.hStdin == INVALID_HANDLE_VALUE || priv.hStdout == INVALID_HANDLE_VALUE) {
        return -2;
    }
    if (!GetConsoleMode(priv.hStdin, &priv.dwOldMode)) {
        return -3;
    }
    DWORD dwNewMode = priv.dwOldMode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    if (!SetConsoleMode(priv.hStdin, dwNewMode)) {
        return -4;
    }
    ctshell_io_t io = {
            .write = windows_shell_write,
            .get_tick = windows_get_tick,
    };
    ctshell_init(ctx, io, &priv);

    return 0;
}

void ctshell_port_windows_deinit(ctshell_ctx_t *ctx) {
    UNREFERENCED_PARAMETER(ctx);
    if (priv.hStdin != INVALID_HANDLE_VALUE) {
        SetConsoleMode(priv.hStdin, priv.dwOldMode);
    }

    priv.hStdin = INVALID_HANDLE_VALUE;
    priv.hStdout = INVALID_HANDLE_VALUE;

    g_ctx = NULL;
}

void ctshell_port_windows_process_input(ctshell_ctx_t *ctx) {
    if (ctx == NULL || ctx != g_ctx) {
        return;
    }
    INPUT_RECORD irInput;
    DWORD dwEventsRead;

    while (PeekConsoleInput(priv.hStdin, &irInput, 1, &dwEventsRead) && dwEventsRead > 0) {
        if (ReadConsoleInput(priv.hStdin, &irInput, 1, &dwEventsRead) && dwEventsRead > 0) {
            if (irInput.EventType == KEY_EVENT && irInput.Event.KeyEvent.bKeyDown) {
                WORD vk = irInput.Event.KeyEvent.wVirtualKeyCode;
                char ch = irInput.Event.KeyEvent.uChar.AsciiChar;
                int handled_as_ansi = 0;

                switch (vk) {
                    case VK_UP:    inject_ansi(ctx, "\x1b[A"); handled_as_ansi = 1; break;
                    case VK_DOWN:  inject_ansi(ctx, "\x1b[B"); handled_as_ansi = 1; break;
                    case VK_RIGHT: inject_ansi(ctx, "\x1b[C"); handled_as_ansi = 1; break;
                    case VK_LEFT:  inject_ansi(ctx, "\x1b[D"); handled_as_ansi = 1; break;
                    case VK_HOME:  inject_ansi(ctx, "\x1b[H"); handled_as_ansi = 1; break;
                    case VK_END:   inject_ansi(ctx, "\x1b[F"); handled_as_ansi = 1; break;
                    case VK_DELETE:inject_ansi(ctx, "\x1b[3~");handled_as_ansi = 1; break;
                }
                if (handled_as_ansi) {
                    continue;
                }
                if (vk == VK_BACK) {
                    ch = '\b';
                } else if (vk == VK_RETURN) {
                    ch = '\r';
                } else if (vk == VK_TAB) {
                    ch = '\t';
                }
                if (ch != 0) {
                    ctshell_input(ctx, ch);
                }
            }
        }
    }
}
