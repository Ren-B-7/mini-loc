#include "include/cli.h"

#include <string.h>

__attribute__((cold)) void loc_config_init(LocConfig* cfg)
{
	memset(cfg, 0, sizeof(LocConfig));
	cfg->output_fmt = LOC_FMT_TERMINAL;
}

__attribute__((cold)) void parse_cli(LocConfig* cfg, int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recurse") == 0) {
			cfg->recurse = true;
		} else if (strcmp(argv[i], "-f") == 0 ||
		 strcmp(argv[i], "--files") == 0) {
			cfg->show_files = true;
		} else if (strcmp(argv[i], "--list-unknown") == 0) {
			cfg->list_unknown = true;
		} else if (strcmp(argv[i], "--verbose") == 0) {
			cfg->verbose = true;
		} else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
			cfg->filter = argv[++i];
		} else if (strcmp(argv[i], "-o") == 0 ||
		 strcmp(argv[i], "--output") == 0) {
			i++;
			if (i < argc) {
				if (strcmp(argv[i], "json") == 0) {
					cfg->output_fmt = LOC_FMT_JSON;
				} else if (strcmp(argv[i], "html") == 0) {
					cfg->output_fmt = LOC_FMT_HTML;
				} else if (strcmp(argv[i], "sql") == 0) {
					cfg->output_fmt = LOC_FMT_SQL;
				}
			}
		}
	}
}
