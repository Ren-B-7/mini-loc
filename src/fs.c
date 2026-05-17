#include "include/fs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "include/types.h"

static void walk_dir_recursive(const char* path, size_t path_len, bool recurse,
 FileCallback cb, void* user)
{
    DIR* d = opendir(path);
    if (!d) {
        return;
    }

    char sub[PATH_BUF];
    memcpy(sub, path, path_len);

#ifdef _WIN32
    sub[path_len] = '\\';
#else
    sub[path_len] = '/';
#endif

    struct dirent* entry;
    while ((entry = readdir(d))) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        size_t nlen = strlen(entry->d_name);
        if (path_len + 1 + nlen >= PATH_BUF) {
            continue;
        }
        memcpy(sub + path_len + 1, entry->d_name, nlen + 1);

#ifdef _DIRENT_HAVE_D_TYPE
        if (entry->d_type == DT_REG) {
            struct stat st;
            if (lstat(sub, &st) == 0) {
                cb(sub, (size_t)st.st_size, user);
            }
        } else if (entry->d_type == DT_DIR && recurse) {
            walk_dir_recursive(sub, path_len + 1 + nlen, recurse, cb, user);
        } else if (entry->d_type == DT_LNK) {
            /* Skip symlinks to avoid infinite loops and double counting */
            continue;
        } else if (entry->d_type == DT_UNKNOWN) {
#else
        /* fallback: always use stat (Windows/MinGW) */
        {
#endif
            struct stat st;
            if (lstat(sub, &st) == 0) {
                if (S_ISREG(st.st_mode)) {
                    cb(sub, (size_t)st.st_size, user);
                } else if (S_ISDIR(st.st_mode) && recurse) {
                    walk_dir_recursive(sub, path_len + 1 + nlen, recurse, cb,
                     user);
                }
            }
        }
    }
    closedir(d);
}

void walk_dir(const char* path, bool recurse, FileCallback cb, void* user)
{
    walk_dir_recursive(path, strlen(path), recurse, cb, user);
}

void process_path(const char* path, bool recurse, FileCallback cb, void* user)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        if (recurse) {
            walk_dir(path, recurse, cb, user);
        } else {
            fprintf(stderr,
             "mini-loc: '%s' is a directory (use -r to recurse)\n", path);
        }
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        return;
    }
    cb(path, (size_t)st.st_size, user);
}
