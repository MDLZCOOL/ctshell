/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell_config.h"
#include "ctshell.h"
#if defined(CONFIG_CTSHELL_USE_FS) && defined(CONFIG_CTSHELL_USE_FS_POSIX)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

/* Wrapper for DIR to keep track of the base path for stat() in readdir() */
typedef struct {
    DIR *dir;
    char path[CONFIG_CTSHELL_FS_PATH_MAX];
} posix_dir_t;

static int map_flags(int flags) {
    if (flags & CTSHELL_O_TRUNC) {
        return O_RDWR | O_CREAT | O_TRUNC;
    } else if (flags & CTSHELL_O_APPEND) {
        return O_RDWR | O_CREAT | O_APPEND;
    } else {
        return O_RDONLY;
    }
}

static int posix_open(const char *path, int flags) {
    int o_flags = map_flags(flags);
    /* 0666 gives read/write permissions for user, group, and others if file is created */
    int fd = open(path, o_flags, 0666);

    if (fd < 0) {
        ctshell_error("Open '%s' failed, errno=%d\r\n", path, errno);
        return -1;
    }
    return fd;
}

static int posix_close(int fd) {
    if (fd >= 0) {
        int res = close(fd);
        return (res == 0) ? 0 : -1;
    }
    return -1;
}

static int posix_read(int fd, void *buf, uint32_t count) {
    if (fd >= 0) {
        ssize_t ret = read(fd, buf, count);
        if (ret >= 0) {
            return (int)ret;
        }
    }
    return -1;
}

static int posix_write(int fd, const void *buf, uint32_t count) {
    if (fd >= 0) {
        ssize_t ret = write(fd, buf, count);
        if (ret >= 0) {
            return (int)ret;
        } else {
            ctshell_error("Write failed, errno=%d\r\n", errno);
            return -1;
        }
    }
    return -1;
}

static int posix_lseek(int fd, long offset, int whence) {
    if (fd < 0) return -1;

    off_t ret = lseek(fd, offset, whence);
    if (ret == (off_t)-1) {
        return -1;
    }
    return 0;
}

static int posix_opendir(const char *path, void **dir_handle) {
    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }

    posix_dir_t *pd = (posix_dir_t *)malloc(sizeof(posix_dir_t));
    if (!pd) {
        closedir(d);
        return -1;
    }

    pd->dir = d;
    strncpy(pd->path, path, sizeof(pd->path) - 1);
    pd->path[sizeof(pd->path) - 1] = '\0';

    *dir_handle = pd;
    return 0;
}

static int posix_readdir(void *dir_handle, ctshell_dirent_t *entry) {
    if (!dir_handle) return -1;

    posix_dir_t *pd = (posix_dir_t *)dir_handle;
    struct dirent *de = readdir(pd->dir);

    if (de) {
        strncpy(entry->name, de->d_name, CONFIG_CTSHELL_FS_NAME_MAX - 1);
        entry->name[CONFIG_CTSHELL_FS_NAME_MAX - 1] = '\0';

        /* Construct full path to use stat() for size and robust type checking */
        char full_path[CONFIG_CTSHELL_FS_PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", pd->path, de->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            entry->size = st.st_size;
            entry->type = S_ISDIR(st.st_mode) ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
        } else {
            entry->size = 0;
#ifdef DT_DIR
            /* Fallback to d_type if stat fails */
            entry->type = (de->d_type == DT_DIR) ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
#else
            entry->type = CTSHELL_FS_TYPE_FILE;
#endif
        }
        return 0;
    }
    return -1;
}

static int posix_closedir(void *dir_handle) {
    if (dir_handle) {
        posix_dir_t *pd = (posix_dir_t *)dir_handle;
        closedir(pd->dir);
        free(pd);
        return 0;
    }
    return -1;
}

static int posix_stat(const char *path, ctshell_dirent_t *info) {
    struct stat st;
    if (stat(path, &st) == 0) {
        info->type = S_ISDIR(st.st_mode) ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
        info->size = st.st_size;
        return 0;
    }
    return -1;
}

static int delete_dir_tree(const char *path) {
    DIR *d = opendir(path);
    size_t path_len = strlen(path);
    int r = -1;

    if (d) {
        struct dirent *p;
        r = 0;
        while (!r && (p = readdir(d))) {
            int r2 = -1;
            char *buf;
            size_t len;

            /* Skip "." and ".." */
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) {
                continue;
            }

            len = path_len + strlen(p->d_name) + 2;
            buf = (char *)malloc(len);
            if (buf) {
                struct stat st;
                snprintf(buf, len, "%s/%s", path, p->d_name);
                if (!stat(buf, &st)) {
                    if (S_ISDIR(st.st_mode)) {
                        r2 = delete_dir_tree(buf);
                    } else {
                        r2 = unlink(buf);
                    }
                }
                free(buf);
            }
            r = r2;
        }
        closedir(d);
    }

    if (!r) {
        r = rmdir(path);
    }
    return r;
}

static int posix_unlink(const char *path) {
    struct stat st;

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            /* If it's a directory, perform a recursive delete */
            if (delete_dir_tree(path) != 0) {
                ctshell_error("unlink: failed to remove dir '%s', errno=%d\r\n", path, errno);
                return -1;
            }
        } else {
            /* If it's a regular file, just unlink it */
            if (unlink(path) != 0) {
                ctshell_error("unlink: failed to remove file '%s', errno=%d\r\n", path, errno);
                return -1;
            }
        }
        return 0;
    }
    return -1;
}

static int posix_mkdir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        ctshell_error("mkdir: '%s' already exists\r\n", path);
        return -1;
    }

    /* 0777 gives read/write/execute permissions */
    if (mkdir(path, 0777) != 0) {
        ctshell_error("mkdir '%s' failed: errno=%d\r\n", path, errno);
        return -1;
    }
    return 0;
}

const ctshell_fs_drv_t posix_drv = {
        .open = posix_open,
        .close = posix_close,
        .read = posix_read,
        .write = posix_write,
        .opendir = posix_opendir,
        .readdir = posix_readdir,
        .closedir = posix_closedir,
        .stat = posix_stat,
        .unlink = posix_unlink,
        .mkdir = posix_mkdir,
        .lseek = posix_lseek,
};

extern void ctshell_fs_init(ctshell_ctx_t *ctx, const ctshell_fs_drv_t *drv);
void ctshell_fs_posix_init(ctshell_ctx_t *ctx) {
    ctshell_fs_init(ctx, &posix_drv);
}
#endif