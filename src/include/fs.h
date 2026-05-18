#ifndef LOC_FS_H
#define LOC_FS_H

#include <stdbool.h>

#include "threading.h"
#include "types.h"

typedef void (*FileCallback)(const char* path, size_t size, void* user);

void walk_dir_task(DirQueue* dq, WorkQueue* wq, size_t* total_bytes,
 bool recurse);
void process_path(const char* path, bool recurse, WorkQueue* wq, DirQueue* dq);

#endif
