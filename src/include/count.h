#ifndef LOC_COUNT_H
#define LOC_COUNT_H

#include "types.h"

Counts count_file(const char* path, int lang_idx);
bool scan_for_end(const char* p, const char* line_end, const char* end,
 size_t end_len);

#endif
