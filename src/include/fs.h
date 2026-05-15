#ifndef LOC_FS_H
#define LOC_FS_H

#include <stdbool.h>

#include "types.h"

typedef void (*FileCallback)(const char* path, size_t size, void* user);

void walk_dir(const char* path, bool recurse, FileCallback cb, void* user);
void process_path(const char* path, bool recurse, FileCallback cb, void* user);

#endif
