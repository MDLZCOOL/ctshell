/*
 * Copyright (c) 2026, MDLZCOOL
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ctshell_config.h"
#include "ctshell.h"
#if defined(CONFIG_CTSHELL_USE_FS) && defined(CONFIG_CTSHELL_USE_FS_WINDOWS)
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_OPEN_FILES  2

typedef struct {
    HANDLE handle;
    uint8_t used;
} windows_file_slot_t;

typedef struct {
    HANDLE find_handle;
    WIN32_FIND_DATAA fdata;
    uint8_t is_first;
} windows_dir_t;

static windows_file_slot_t file_pool[MAX_OPEN_FILES];

static void convert_path(const char *in_path, char *out_path, size_t max_len) {
    const char *base = ".";
    const char *p = in_path;

    while (*p == '/' || *p == '\\') {
        p++;
    }
    if (*p == '\0') {
        snprintf(out_path, max_len, "%s", base);
    } else {
        snprintf(out_path, max_len, "%s\\%s", base, p);
    }
    for (size_t i = 0; out_path[i] != '\0'; i++) {
        if (out_path[i] == '/') {
            out_path[i] = '\\';
        }
    }
}

static int windows_open(const char *path, int flags) {
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

    char win_path[MAX_PATH];
    convert_path(path, win_path, sizeof(win_path));

    DWORD desired_access = 0;
    DWORD creation_disposition = OPEN_EXISTING;

    if (flags & CTSHELL_O_TRUNC) {
        desired_access = GENERIC_READ | GENERIC_WRITE;
        creation_disposition = CREATE_ALWAYS;
    } else if (flags & CTSHELL_O_APPEND) {
        desired_access = GENERIC_READ | GENERIC_WRITE;
        creation_disposition = OPEN_ALWAYS;
    } else {
        desired_access = GENERIC_READ;
    }

    HANDLE hFile = CreateFileA(
        win_path,
        desired_access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        creation_disposition,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        ctshell_error("Open '%s' failed, err=%lu\r\n", win_path, GetLastError());
        return -1;
    }

    file_pool[fd].handle = hFile;
    file_pool[fd].used = 1;
    if (flags & CTSHELL_O_APPEND) {
        LARGE_INTEGER distance;
        distance.QuadPart = 0;
        SetFilePointerEx(hFile, distance, NULL, FILE_END);
    }

    return fd;
}

static int windows_close(int fd) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        BOOL res = CloseHandle(file_pool[fd].handle);
        file_pool[fd].used = 0;
        file_pool[fd].handle = INVALID_HANDLE_VALUE;
        return res ? 0 : -1;
    }
    return -1;
}

static int windows_read(int fd, void *buf, uint32_t count) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        DWORD bytes_read = 0;
        if (ReadFile(file_pool[fd].handle, buf, count, &bytes_read, NULL)) {
            return (int)bytes_read;
        }
    }
    return -1;
}

static int windows_write(int fd, const void *buf, uint32_t count) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_pool[fd].used) {
        DWORD bytes_written = 0;
        if (WriteFile(file_pool[fd].handle, buf, count, &bytes_written, NULL)) {
            return (int)bytes_written;
        }
        ctshell_error("Write failed, err=%lu\r\n", GetLastError());
    }
    return -1;
}

static int windows_lseek(int fd, long offset, int whence) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_pool[fd].used) return -1;

    DWORD move_method;
    switch (whence) {
        case SEEK_SET: move_method = FILE_BEGIN; break;
        case SEEK_CUR: move_method = FILE_CURRENT; break;
        case SEEK_END: move_method = FILE_END; break;
        default: return -1;
    }

    LARGE_INTEGER distance;
    distance.QuadPart = offset;

    if (SetFilePointerEx(file_pool[fd].handle, distance, NULL, move_method)) {
        return 0;
    }
    return -1;
}

static int windows_opendir(const char *path, void **dir_handle) {
    char win_path[MAX_PATH];
    convert_path(path, win_path, sizeof(win_path));
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", win_path);
    windows_dir_t *dir = (windows_dir_t *)malloc(sizeof(windows_dir_t));
    if (!dir) return -1;
    dir->find_handle = FindFirstFileA(search_path, &dir->fdata);
    if (dir->find_handle == INVALID_HANDLE_VALUE) {
        free(dir);
        return -1;
    }
    dir->is_first = 1;
    *dir_handle = dir;
    return 0;
}

static int windows_readdir(void *dir_handle, ctshell_dirent_t *entry) {
    if (!dir_handle) return -1;
    windows_dir_t *dir = (windows_dir_t *)dir_handle;
    if (dir->is_first) {
        dir->is_first = 0;
    } else {
        if (!FindNextFileA(dir->find_handle, &dir->fdata)) {
            return -1;
        }
    }

    strncpy(entry->name, dir->fdata.cFileName, CONFIG_CTSHELL_FS_NAME_MAX - 1);
    entry->name[CONFIG_CTSHELL_FS_NAME_MAX - 1] = '\0';
    entry->size = ((uint64_t)dir->fdata.nFileSizeHigh << 32) | dir->fdata.nFileSizeLow;
    entry->type = (dir->fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;

    return 0;
}

static int windows_closedir(void *dir_handle) {
    if (!dir_handle) return -1;

    windows_dir_t *dir = (windows_dir_t *)dir_handle;
    BOOL res = FindClose(dir->find_handle);
    free(dir);

    return res ? 0 : -1;
}

static int windows_stat(const char *path, ctshell_dirent_t *info) {
    char win_path[MAX_PATH];
    convert_path(path, win_path, sizeof(win_path));

    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (GetFileAttributesExA(win_path, GetFileExInfoStandard, &attr)) {
        info->type = (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? CTSHELL_FS_TYPE_DIR : CTSHELL_FS_TYPE_FILE;
        info->size = ((uint64_t)attr.nFileSizeHigh << 32) | attr.nFileSizeLow;
        return 0;
    }
    return -1;
}

static int delete_dir_tree(const char *path) {
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", path);

    WIN32_FIND_DATAA fdata;
    HANDLE hFind = FindFirstFileA(search_path, &fdata);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fdata.cFileName, ".") == 0 || strcmp(fdata.cFileName, "..") == 0) {
                continue;
            }
            char sub_path[MAX_PATH];
            snprintf(sub_path, sizeof(sub_path), "%s\\%s", path, fdata.cFileName);
            if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_dir_tree(sub_path);
            } else {
                DeleteFileA(sub_path);
            }
        } while (FindNextFileA(hFind, &fdata));
        FindClose(hFind);
    }

    return RemoveDirectoryA(path) ? 0 : -1;
}

static int windows_unlink(const char *path) {
    char win_path[MAX_PATH];
    convert_path(path, win_path, sizeof(win_path));
    DWORD attr = GetFileAttributesA(win_path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return -1;
    }

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        if (delete_dir_tree(win_path) != 0) {
            ctshell_error("unlink: failed to remove dir '%s', err=%lu\r\n", win_path, GetLastError());
            return -1;
        }
    } else {
        if (!DeleteFileA(win_path)) {
            ctshell_error("unlink: failed to remove file '%s', err=%lu\r\n", win_path, GetLastError());
            return -1;
        }
    }
    return 0;
}

static int windows_mkdir(const char *path) {
    char win_path[MAX_PATH];
    convert_path(path, win_path, sizeof(win_path));

    DWORD attr = GetFileAttributesA(win_path);
    if (attr != INVALID_FILE_ATTRIBUTES) {
        ctshell_error("mkdir: '%s' already exists\r\n", path);
        return -1;
    }

    if (!CreateDirectoryA(win_path, NULL)) {
        ctshell_error("mkdir '%s' failed, err=%lu\r\n", win_path, GetLastError());
        return -1;
    }
    return 0;
}

const ctshell_fs_drv_t windows_drv = {
        .open = windows_open,
        .close = windows_close,
        .read = windows_read,
        .write = windows_write,
        .opendir = windows_opendir,
        .readdir = windows_readdir,
        .closedir = windows_closedir,
        .stat = windows_stat,
        .unlink = windows_unlink,
        .mkdir = windows_mkdir,
        .lseek = windows_lseek,
};

extern void ctshell_fs_init(ctshell_ctx_t *ctx, const ctshell_fs_drv_t *drv);
void ctshell_fs_windows_init(ctshell_ctx_t *ctx) {
    memset(file_pool, 0, sizeof(file_pool));
    ctshell_fs_init(ctx, &windows_drv);
}
#endif
