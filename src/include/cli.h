#ifndef LOC_CLI_H
#define LOC_CLI_H

#include <stdbool.h>

#include "types.h"

typedef struct {
	bool recurse;
	bool show_files;
	bool list_unknown;
	bool verbose;

	char* filter;

	LocOutputFormat output_fmt;
} LocConfig;

void loc_config_init(LocConfig* cfg);
void parse_cli(LocConfig* cfg, int argc, char** argv);

#endif
