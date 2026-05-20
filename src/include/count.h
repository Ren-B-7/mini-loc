#ifndef LOC_COUNT_H
#define LOC_COUNT_H

#include "types.h"

Counts count_file(const char* path, int lang_idx);
Counts count_file_complexity(const char* path, int lang_idx);

#endif
