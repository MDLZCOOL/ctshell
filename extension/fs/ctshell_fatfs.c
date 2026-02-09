/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell_config.h"
#if defined(CTSHELL_USE_FS) && defined(CTSHELL_USE_FS_FATFS)
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "ff.h"
#include "ctshell.h"

#define MAX_OPEN_FILES  2

typedef struct {
    FIL fil;
    uint8_t used;
} fatfs_file_slot_t;

static FATFS fs;
static DIR dir_obj;
static FILINFO fno;
static fatfs_file_slot_t file_pool[MAX_OPEN_FILES];

static void mount_fs(void) {
    f_mount(NULL, "", 0);
    FRESULT res = f_mount(&fs, "", 1);
    if (res != FR_OK) {
        ctshell_error("Mount failed: %d\r\n", res);
    }
}

static BYTE map_flags(int flags) {
    if (flags & CTSHELL_O_TRUNC) {
        return FA_WRITE | FA_CREATE_ALWAYS;
    } else if (flags & CTSHELL_O_APPEND) {
        return FA_WRITE | FA_OPEN_ALWAYS;
    } else {
        return FA_READ;
    }
}

static const char *clean_path(const char *path) {
    const char *p = path;
//    if (p[0] == '/') p++;
//    if (p[0] == '\0' && path[0] == '/') return "";
    return p;
}

static int fatfs_open(const char *path, int flags) {
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
    memset(&file_pool[fd].fil, 0, sizeof(FIL));
    const char *real_path = clean_path(path);
    BYTE mode = map_flags(flags);
    FRESULT res = f_open(&file_pool[fd].fil, real_path, mode);
    if (res == FR_DISK_ERR || res == FR_NOT_READY) {
        mount_fs();
        res = f_open(&file_pool[fd].fil, real_path, mode);
    }
    if (res == FR_OK) {
        file_pool[fd].used = 1;
        if ((flags & CTSHELL_O_APPEND) && f_size(&file_pool[fd].fil) > 0) {
            f_lseek(&file_pool[fd].fil, f_size(&file_pool[fd].fil));
        }
        return fd;
    }
    if (res != FR_NO_FILE && res != FR_NO_PATH) {
        ctshell_error("Open '%s' failed, ret=%d\r\n", real_path, res);
    }
    return -1;
}

static int fatfs_close(int fd) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        FRESULT res = f_close(&file_pool[fd].fil);
        file_pool[fd].used = 0;
        memset(&file_pool[fd].fil, 0, sizeof(FIL));
        return (res == FR_OK) ? 0 : -1;
    }
    return -1;
}

static int fatfs_read(int fd, void *buf, uint32_t count) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        UINT br;
        if (f_read(&file_pool[fd].fil, buf, count, &br) == FR_OK) {
            return (int) br;
        }
    }
    return -1;
}

static int fatfs_write(int fd, const void *buf, uint32_t count) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        UINT bw;
        FRESULT res = f_write(&file_pool[fd].fil, buf, count, &bw);
        if (res != FR_OK) {
            ctshell_error("Write failed, ret=%d\r\n", res);
            return -1;
        }
        return (int) bw;
    }
    return -1;
}

static int fatfs_lseek(int fd, long offset, int whence) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_pool[fd].used) return -1;

    FIL *fp = &file_pool[fd].fil;
    FSIZE_t dest = 0;
    FSIZE_t size = f_size(fp);
    FSIZE_t curr = f_tell(fp);

    switch (whence) {
        case SEEK_SET:
            dest = (FSIZE_t) offset;
            break;
        case SEEK_CUR:
            dest = curr + offset;
            break;
        case SEEK_END:
            dest = size + offset;
            break;
        default:
            return -1;
    }

    if (f_lseek(fp, dest) == FR_OK) return 0;
    return -1;
}

static int fatfs_opendir(const char *path, void **dir_handle) {
    const char *real_path = clean_path(path);

    FRESULT res = f_opendir(&dir_obj, real_path);
    if (res == FR_DISK_ERR || res == FR_NOT_READY) {
        mount_fs();
        res = f_opendir(&dir_obj, real_path);
    }
    if (res == FR_OK) {
        *dir_handle = &dir_obj;
        return 0;
    }
    return -1;
}

static int fatfs_readdir(void *dir_handle, ctshell_dirent_t *entry) {
    if (f_readdir((DIR *) dir_handle, &fno) == FR_OK && fno.fname[0] != 0) {
        strncpy(entry->name, fno.fname, CTSHELL_FS_NAME_MAX - 1);
        entry->name[CTSHELL_FS_NAME_MAX - 1] = '\0';
        entry->size = fno.fsize;
        entry->type = (fno.fattrib & AM_DIR) ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
        return 0;
    }
    return -1;
}

static int fatfs_closedir(void *dir_handle) {
    f_closedir((DIR *) dir_handle);
    return 0;
}

static int fatfs_stat(const char *path, ctshell_dirent_t *info) {
    if (strcmp(path, "/") == 0 || strcmp(path, "\\") == 0) {
        info->type = CTSHELL_FS_TYPE_DIR;
        info->size = 0;
        return 0;
    }
    const char *real_path = clean_path(path);
    FILINFO temp_fno;
    FRESULT res = f_stat(real_path, &temp_fno);
    if (res == FR_DISK_ERR || res == FR_NOT_READY) {
        mount_fs();
        res = f_stat(real_path, &temp_fno);
    }

    if (res == FR_OK) {
        info->type = (temp_fno.fattrib & AM_DIR) ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
        info->size = temp_fno.fsize;
        return 0;
    }
    return -1;
}

static FRESULT delete_node(const char *path, UINT sz_buff, FILINFO* fno)
{
    FRESULT res;
    DIR dir;
    UINT i, j;

    res = f_opendir(&dir, path);
    if (res != FR_OK) return res;
    for (i = 0; path[i]; i++) ;
    while (1) {
        res = f_readdir(&dir, fno);
        if (res != FR_OK || fno->fname[0] == 0) break;
        if (fno->fname[0] == '.') continue;
        j = 0;
        do {
            if (i + 1 + j >= sz_buff) {
                res = FR_NOT_ENOUGH_CORE;
                break;
            }
            ((char*)path)[i + 1 + j] = fno->fname[j];
            j++;
        } while (fno->fname[j-1]);
        ((char*)path)[i] = '/';
        if (fno->fattrib & AM_DIR) {
            res = delete_node(path, sz_buff, fno);
        } else {
            res = f_unlink(path);
        }
        ((char*)path)[i] = 0;
        if (res != FR_OK) break;
    }
    f_closedir(&dir);
    if (res == FR_OK) {
        res = f_unlink(path);
    }

    return res;
}

static int fatfs_unlink(const char *path) {
    FRESULT res;
    FILINFO fno;

    res = f_unlink(path);
    if (res == FR_DENIED) {
        if (f_stat(path, &fno) == FR_OK) {
            if (fno.fattrib & AM_DIR) {
                char *work_path = malloc(CTSHELL_FS_PATH_MAX);
                if (work_path) {
                    strncpy(work_path, path, CTSHELL_FS_PATH_MAX);
                    res = delete_node(work_path, CTSHELL_FS_PATH_MAX, &fno);
                    free(work_path);
                } else {
                    ctshell_error("unlink: out of memory\r\n");
                    return -1;
                }
            }
        }
    }

    if (res != FR_OK) {
        return -1;
    }
    return 0;
}

static int fatfs_mkdir(const char *path) {
    FILINFO fno;
    if (f_stat(path, &fno) == FR_OK) {
        ctshell_error("mkdir: '%s' already exists\r\n", path);
        return -1;
    }
    FRESULT res = f_mkdir(path);
    if (res != FR_OK) {
        ctshell_error("mkdir '%s' failed: %d\r\n", path, res);
        return -1;
    }
    return 0;
}

const ctshell_fs_drv_t fatfs_drv = {
        .open = fatfs_open,
        .close = fatfs_close,
        .read = fatfs_read,
        .write = fatfs_write,
        .opendir = fatfs_opendir,
        .readdir = fatfs_readdir,
        .closedir = fatfs_closedir,
        .stat = fatfs_stat,
        .unlink = fatfs_unlink,
        .mkdir = fatfs_mkdir,
        .lseek = fatfs_lseek,
};

extern void ctshell_fs_init(ctshell_ctx_t *ctx, const ctshell_fs_drv_t *drv);
void ctshell_fatfs_init(ctshell_ctx_t *ctx) {
    memset(file_pool, 0, sizeof(file_pool));
    mount_fs();
    ctshell_fs_init(ctx, &fatfs_drv);
}
#endif
