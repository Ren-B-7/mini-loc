#ifndef LOC_CLI_H
#define LOC_CLI_H

#include <stdbool.h>

#include "types.h"

typedef struct {
    bool recurse;
    bool show_files;
    bool list_unknown;
    bool verbose;
    bool complexity_check;

    char* filter;
    char* lang_load_path;
    char* lang_append_path;

    LocOutputFormat output_fmt;
    LocSortOrder sort_order;
    size_t total_bytes;
    bool no_bytes;
} LocConfig;

void loc_config_init(LocConfig* cfg);
void parse_cli(LocConfig* cfg, int argc, char** argv);

#endif
