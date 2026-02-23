/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell_config.h"
#if defined(CONFIG_CTSHELL_USE_FS) && defined(CONFIG_CTSHELL_USE_FS_ARDUINO_SD)
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

extern "C" {
    #include "ctshell.h"
}

#define MAX_OPEN_FILES  2

typedef struct {
    File file;
    bool used;
} arduino_sd_file_slot_t;

typedef struct {
    File dir;
} arduino_sd_dir_t;

static arduino_sd_file_slot_t file_pool[MAX_OPEN_FILES];

static int arduino_sd_open(const char *path, int flags) {
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

    uint8_t mode = FILE_READ;
    if (flags & CTSHELL_O_TRUNC) {
        mode = FILE_WRITE;
        if (SD.exists(path)) {
            SD.remove(path);
        }
    } else if (flags & CTSHELL_O_APPEND) {
        mode = FILE_WRITE;
    }
    File f = SD.open(path, mode);
    if (!f) {
        ctshell_error("Open '%s' failed\r\n", path);
        return -1;
    }

    file_pool[fd].file = f;
    file_pool[fd].used = true;

    return fd;
}

static int arduino_sd_close(int fd) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        file_pool[fd].file.close();
        file_pool[fd].used = false;
        return 0;
    }
    return -1;
}

static int arduino_sd_read(int fd, void *buf, uint32_t count) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        int bytes_read = file_pool[fd].file.read(buf, count);
        if (bytes_read >= 0) {
            return bytes_read;
        }
    }
    return -1;
}

static int arduino_sd_write(int fd, const void *buf, uint32_t count) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        size_t bytes_written = file_pool[fd].file.write((const uint8_t *)buf, count);
        return (int)bytes_written;
    }
    return -1;
}

static int arduino_sd_lseek(int fd, long offset, int whence) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_pool[fd].used) return -1;

    File *f = &file_pool[fd].file;
    uint32_t target_pos = 0;

    switch (whence) {
        case SEEK_SET:
            target_pos = offset;
            break;
        case SEEK_CUR:
            target_pos = f->position() + offset;
            break;
        case SEEK_END:
            target_pos = f->size() + offset;
            break;
        default:
            return -1;
    }

    if (f->seek(target_pos)) {
        return 0;
    }
    return -1;
}

static int arduino_sd_opendir(const char *path, void **dir_handle) {
    File dir = SD.open(path);

    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return -1;
    }

    arduino_sd_dir_t *d = new arduino_sd_dir_t();
    d->dir = dir;
    d->dir.rewindDirectory();
    *dir_handle = (void *)d;
    return 0;
}

static int arduino_sd_readdir(void *dir_handle, ctshell_dirent_t *entry) {
    if (!dir_handle) return -1;

    arduino_sd_dir_t *d = (arduino_sd_dir_t *)dir_handle;
    File next_file = d->dir.openNextFile();

    if (next_file) {
        const char *name = next_file.name();
        const char *slash = strrchr(name, '/');
        if (slash) name = slash + 1;
        strncpy(entry->name, name, CONFIG_CTSHELL_FS_NAME_MAX - 1);
        entry->name[CONFIG_CTSHELL_FS_NAME_MAX - 1] = '\0';
        entry->size = next_file.size();
        entry->type = next_file.isDirectory() ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
        next_file.close();
        return 0;
    }

    return -1;
}

static int arduino_sd_closedir(void *dir_handle) {
    if (!dir_handle) return -1;

    arduino_sd_dir_t *d = (arduino_sd_dir_t *)dir_handle;
    d->dir.close();
    delete d;

    return 0;
}

static int arduino_sd_stat(const char *path, ctshell_dirent_t *info) {
    if (strcmp(path, "/") == 0) {
        info->type = CTSHELL_FS_TYPE_DIR;
        info->size = 0;
        return 0;
    }

    File f = SD.open(path, FILE_READ);
    if (f) {
        info->type = f.isDirectory() ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
        info->size = f.size();
        f.close();
        return 0;
    }
    return -1;
}

static int delete_dir_tree(const char *path) {
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return -1;
    }

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        String entry_name = String(entry.name());
        bool is_dir = entry.isDirectory();
        entry.close();

        String sub_path = String(path);
        if (!sub_path.endsWith("/")) {
            sub_path += "/";
        }
        if (entry_name.startsWith("/")) {
            sub_path = entry_name;
        } else {
            sub_path += entry_name;
        }
        if (is_dir) {
            delete_dir_tree(sub_path.c_str());
        } else {
            SD.remove(sub_path.c_str());
        }
    }
    dir.close();

    return SD.rmdir(path) ? 0 : -1;
}

static int arduino_sd_unlink(const char *path) {
    if (strcmp(path, "/") == 0) return -1;

    File f = SD.open(path, FILE_READ);
    if (!f) return -1;

    bool is_dir = f.isDirectory();
    f.close();

    if (is_dir) {
        return delete_dir_tree(path);
    } else {
        return SD.remove(path) ? 0 : -1;
    }
}

static int arduino_sd_mkdir(const char *path) {
    if (SD.exists(path)) {
        ctshell_error("mkdir: '%s' already exists\r\n", path);
        return -1;
    }

    return SD.mkdir(path) ? 0 : -1;
}

const ctshell_fs_drv_t arduino_sd_drv = {
        .open = arduino_sd_open,
        .close = arduino_sd_close,
        .read = arduino_sd_read,
        .write = arduino_sd_write,
        .opendir = arduino_sd_opendir,
        .readdir = arduino_sd_readdir,
        .closedir = arduino_sd_closedir,
        .stat = arduino_sd_stat,
        .unlink = arduino_sd_unlink,
        .mkdir = arduino_sd_mkdir,
        .lseek = arduino_sd_lseek,
};

extern "C" void ctshell_fs_init(ctshell_ctx_t *ctx, const ctshell_fs_drv_t *drv);
extern "C" void ctshell_fs_arduino_sd_init(ctshell_ctx_t *ctx) {
    for(int i = 0; i < MAX_OPEN_FILES; i++) {
        file_pool[i].used = false;
    }
    ctshell_fs_init(ctx, &arduino_sd_drv);
}
#endif
