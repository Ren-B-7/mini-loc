#include "include/fs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "include/threading.h"
#include "include/types.h"

#ifdef _WIN32
#define LSTAT stat
#else
#define LSTAT lstat
#endif

static void walk_dir_recursive_hybrid(const char* path, size_t path_len,
 bool recurse, WorkQueue* wq, DirQueue* dq, size_t* total_bytes, int depth)
{
    DIR* d = opendir(path);
    if (!d) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(d))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        size_t nlen = strlen(entry->d_name);
        if (path_len + 1 + nlen >= PATH_BUF) {
            continue;
        }

        char sub[PATH_BUF];
        memcpy(sub, path, path_len);
        sub[path_len] = '/';
        memcpy(sub + path_len + 1, entry->d_name, nlen + 1);

        struct stat st;
        if (LSTAT(sub, &st) != 0) {
            continue;
        }

        if (S_ISREG(st.st_mode)) {
            __atomic_fetch_add(total_bytes, (size_t)st.st_size,
             __ATOMIC_RELAXED);
            wq_push(wq, strdup(sub));
        } else if (S_ISDIR(st.st_mode) && recurse) {
            if (depth < 2) {
                dq_push(dq, sub, depth + 1);
            } else {
                walk_dir_recursive_hybrid(sub, path_len + 1 + nlen, recurse, wq,
                 dq, total_bytes, depth + 1);
            }
        }
    }
    closedir(d);
}

void walk_dir_task(DirQueue* dq, WorkQueue* wq, size_t* total_bytes,
 bool recurse)
{
    char path[PATH_BUF];
    int depth;
    while (dq_pop(dq, path, &depth)) {
        walk_dir_recursive_hybrid(path, strlen(path), recurse, wq, dq,
         total_bytes, depth);
    }
}

void process_path(const char* path, bool recurse, WorkQueue* wq, DirQueue* dq)
{
    (void)recurse;
    struct stat st;
    if (LSTAT(path, &st) != 0) {
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        dq_push(dq, path, 0);
    } else if (S_ISREG(st.st_mode)) {
        wq_push(wq, strdup(path));
    }
}
