/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell_config.h"
#include "ctshell.h"
#if defined(CONFIG_CTSHELL_USE_FS) && defined(CONFIG_CTSHELL_USE_FS_LITTLEFS)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lfs.h"

#define MAX_OPEN_FILES  2

typedef struct {
    lfs_file_t file;
    uint8_t used;
} littlefs_file_slot_t;

static lfs_t *lfs_handle = NULL;
static littlefs_file_slot_t file_pool[MAX_OPEN_FILES];

static int map_flags(int flags) {
    if (flags & CTSHELL_O_TRUNC) {
        return LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC;
    } else if (flags & CTSHELL_O_APPEND) {
        return LFS_O_RDWR | LFS_O_CREAT | LFS_O_APPEND;
    } else {
        return LFS_O_RDONLY;
    }
}

static int littlefs_open(const char *path, int flags) {
    if (!lfs_handle) return -1;

    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_pool[i].used) {
            fd = i;
            break;
        }
    }
    if (fd < 0) {
        ctshell_error("Too many opened files\r\n");
        return -1;
    }

    memset(&file_pool[fd].file, 0, sizeof(lfs_file_t));
    int lfs_flags = map_flags(flags);

    int res = lfs_file_open(lfs_handle, &file_pool[fd].file, path, lfs_flags);
    if (res == LFS_ERR_OK) {
        file_pool[fd].used = 1;
        return fd;
    }

    ctshell_error("Open '%s' failed, ret=%d\r\n", path, res);
    return -1;
}

static int littlefs_close(int fd) {
    if (!lfs_handle) return -1;

    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        int res = lfs_file_close(lfs_handle, &file_pool[fd].file);
        file_pool[fd].used = 0;
        return (res == LFS_ERR_OK) ? 0 : -1;
    }
    return -1;
}

static int littlefs_read(int fd, void *buf, uint32_t count) {
    if (!lfs_handle) return -1;

    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        lfs_ssize_t res = lfs_file_read(lfs_handle, &file_pool[fd].file, buf, count);
        if (res >= 0) {
            return (int)res;
        }
    }
    return -1;
}

static int littlefs_write(int fd, const void *buf, uint32_t count) {
    if (!lfs_handle) return -1;

    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        lfs_ssize_t res = lfs_file_write(lfs_handle, &file_pool[fd].file, buf, count);
        if (res >= 0) {
            return (int)res;
        }
        ctshell_error("Write failed, ret=%d\r\n", (int)res);
    }
    return -1;
}

static int littlefs_lseek(int fd, long offset, int whence) {
    if (!lfs_handle) return -1;
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_pool[fd].used) return -1;

    int lfs_whence;
    switch (whence) {
        case SEEK_SET: lfs_whence = LFS_SEEK_SET; break;
        case SEEK_CUR: lfs_whence = LFS_SEEK_CUR; break;
        case SEEK_END: lfs_whence = LFS_SEEK_END; break;
        default: return -1;
    }

    lfs_soff_t res = lfs_file_seek(lfs_handle, &file_pool[fd].file, offset, lfs_whence);
    return (res >= 0) ? 0 : -1;
}

static int littlefs_opendir(const char *path, void **dir_handle) {
    if (!lfs_handle) return -1;

    lfs_dir_t *dir = malloc(sizeof(lfs_dir_t));
    if (!dir) return -1;

    int res = lfs_dir_open(lfs_handle, dir, path);
    if (res == LFS_ERR_OK) {
        *dir_handle = dir;
        return 0;
    }

    free(dir);
    return -1;
}

static int littlefs_readdir(void *dir_handle, ctshell_dirent_t *entry) {
    if (!lfs_handle || !dir_handle) return -1;

    lfs_dir_t *dir = (lfs_dir_t *)dir_handle;
    struct lfs_info info;

    int res = lfs_dir_read(lfs_handle, dir, &info);
    if (res > 0) {
        strncpy(entry->name, info.name, CONFIG_CTSHELL_FS_NAME_MAX - 1);
        entry->name[CONFIG_CTSHELL_FS_NAME_MAX - 1] = '\0';
        entry->size = info.size;
        entry->type = (info.type == LFS_TYPE_DIR) ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
        return 0;
    }

    return -1;
}

static int littlefs_closedir(void *dir_handle) {
    if (!lfs_handle || !dir_handle) return -1;

    lfs_dir_t *dir = (lfs_dir_t *)dir_handle;
    int res = lfs_dir_close(lfs_handle, dir);
    free(dir);

    return (res == LFS_ERR_OK) ? 0 : -1;
}

static int littlefs_stat(const char *path, ctshell_dirent_t *info) {
    if (!lfs_handle) return -1;

    if (strcmp(path, "/") == 0 || strcmp(path, "\\") == 0) {
        info->type = CTSHELL_FS_TYPE_DIR;
        info->size = 0;
        return 0;
    }

    struct lfs_info lfs_info;
    int res = lfs_stat(lfs_handle, path, &lfs_info);
    if (res == LFS_ERR_OK) {
        info->type = (lfs_info.type == LFS_TYPE_DIR) ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
        info->size = lfs_info.size;
        return 0;
    }
    return -1;
}

static int delete_dir_tree(const char *path) {
    lfs_dir_t dir;
    struct lfs_info info;

    int res = lfs_dir_open(lfs_handle, &dir, path);
    if (res < 0) return res;

    while (lfs_dir_read(lfs_handle, &dir, &info) > 0) {
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }

        char *sub_path = malloc(CONFIG_CTSHELL_FS_PATH_MAX);
        if (!sub_path) {
            lfs_dir_close(lfs_handle, &dir);
            return LFS_ERR_NOMEM;
        }

        snprintf(sub_path, CONFIG_CTSHELL_FS_PATH_MAX, "%s/%s", path, info.name);

        if (info.type == LFS_TYPE_DIR) {
            res = delete_dir_tree(sub_path);
        } else {
            res = lfs_remove(lfs_handle, sub_path);
        }
        free(sub_path);

        if (res < 0) break;
    }
    lfs_dir_close(lfs_handle, &dir);

    if (res >= 0) {
        res = lfs_remove(lfs_handle, path);
    }
    return res;
}

static int littlefs_unlink(const char *path) {
    if (!lfs_handle) return -1;

    struct lfs_info info;
    int res = lfs_stat(lfs_handle, path, &info);

    if (res == LFS_ERR_OK) {
        if (info.type == LFS_TYPE_DIR) {
            res = delete_dir_tree(path);
        } else {
            res = lfs_remove(lfs_handle, path);
        }
    }

    if (res != LFS_ERR_OK) {
        ctshell_error("unlink '%s' failed, ret=%d\r\n", path, res);
        return -1;
    }
    return 0;
}

static int littlefs_mkdir(const char *path) {
    if (!lfs_handle) return -1;

    int res = lfs_mkdir(lfs_handle, path);
    if (res == LFS_ERR_OK) {
        return 0;
    }

    if (res == LFS_ERR_EXIST) {
        ctshell_error("mkdir: '%s' already exists\r\n", path);
    } else {
        ctshell_error("mkdir '%s' failed: %d\r\n", path, res);
    }
    return -1;
}

const ctshell_fs_drv_t littlefs_drv = {
        .open = littlefs_open,
        .close = littlefs_close,
        .read = littlefs_read,
        .write = littlefs_write,
        .opendir = littlefs_opendir,
        .readdir = littlefs_readdir,
        .closedir = littlefs_closedir,
        .stat = littlefs_stat,
        .unlink = littlefs_unlink,
        .mkdir = littlefs_mkdir,
        .lseek = littlefs_lseek,
};

extern void ctshell_fs_init(ctshell_ctx_t *ctx, const ctshell_fs_drv_t *drv);
void ctshell_fs_littlefs_init(ctshell_ctx_t *ctx, lfs_t *lfs) {
    if (!lfs) return;

    lfs_handle = lfs;
    memset(file_pool, 0, sizeof(file_pool));

    ctshell_fs_init(ctx, &littlefs_drv);
}
#endif
