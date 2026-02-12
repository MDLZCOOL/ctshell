/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell_posix.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <stdint.h>

typedef struct {
    struct termios old_termios;
} ctshell_posix_priv_t;

static ctshell_ctx_t *g_ctx;
static ctshell_posix_priv_t priv;

static void inject_ansi(ctshell_ctx_t *ctx, const char *ansi_seq) {
    while (*ansi_seq) {
        ctshell_input(ctx, *ansi_seq++);
    }
}

static void posix_shell_write(const char *str, uint16_t len, void *p) {
    (void) p;
    write(STDOUT_FILENO, str, len);
}

static uint32_t posix_get_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int ctshell_posix_init(ctshell_ctx_t *ctx) {
    if (ctx == NULL) {
        return -1;
    }
    g_ctx = ctx;

    if (tcgetattr(STDIN_FILENO, &priv.old_termios) < 0) {
        return -2;
    }

    struct termios new_termios = priv.old_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios) < 0) {
        return -3;
    }

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &priv.old_termios);
        return -4;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == -1) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &priv.old_termios);
        return -5;
    }


    ctshell_io_t io = {
            .write = posix_shell_write,
            .get_tick = posix_get_tick,
    };
    ctshell_init(ctx, io, &priv);

    return 0;
}

void ctshell_posix_deinit(ctshell_ctx_t *ctx) {
    (void) ctx;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &priv.old_termios);
    g_ctx = NULL;
}

void ctshell_posix_process_input(ctshell_ctx_t *ctx) {
    if (ctx == NULL || ctx != g_ctx) {
        return;
    }

    char ch;
    while (read(STDIN_FILENO, &ch, 1) == 1) {
        if (ch == '\x1b') {
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) {
                ctshell_input(ctx, ch);
                continue;
            }
            if (read(STDIN_FILENO, &seq[1], 1) != 1) {
                ctshell_input(ctx, ch);
                ctshell_input(ctx, seq[0]);
                continue;
            }

            if (seq[0] == '[') {
                if (seq[1] >= 'A' && seq[1] <= 'D') {
                    switch (seq[1]) {
                        case 'A':
                            inject_ansi(ctx, "\x1b[A");
                            break;
                        case 'B':
                            inject_ansi(ctx, "\x1b[B");
                            break;
                        case 'C':
                            inject_ansi(ctx, "\x1b[C");
                            break;
                        case 'D':
                            inject_ansi(ctx, "\x1b[D");
                            break;
                    }
                } else if (seq[1] == 'H') {
                    inject_ansi(ctx, "\x1b[H");
                } else if (seq[1] == 'F') {
                    inject_ansi(ctx, "\x1b[F");
                } else if (seq[1] == '3' && read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                    inject_ansi(ctx, "\x1b[3~");
                }
            }
        } else if (ch == 127) {
            ctshell_input(ctx, '\b');
        } else {
            ctshell_input(ctx, ch);
        }
    }
}
