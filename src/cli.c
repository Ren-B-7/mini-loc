#include "include/cli.h"

#include <stdio.h>
#include <string.h>

__attribute__((cold)) void loc_config_init(LocConfig* cfg)
{
	memset(cfg, 0, sizeof(LocConfig));
	cfg->output_fmt = LOC_FMT_TERMINAL;
	cfg->sort_order = LOC_SORT_TOTAL;
}

__attribute__((cold)) void print_help(void)
{
	printf("mini-loc — a fast lines-of-code counter\n\n");
	printf("Usage: mini-loc [OPTIONS] [PATHS...]\n\n");
	printf("Options:\n");
	printf("  -r, --recurse       Recurse into directories\n");
	printf("  -f, --files         Show per-file results\n");
	printf("  -s, --sort <ORDER>  Sort by: total, code, comment, blank, "
	       "files\n");
	printf("  -o, --output <FMT>  Output format: terminal, json, html, sql\n");
	printf("  -l, --load <PATH>   Load custom language definitions\n");
	printf("  -a, --append <PATH> Append custom language definitions\n");
	printf("  --list-unknown      List files with unknown extensions\n");
	printf("  --filter <EXTS>     Only process these extensions (comma-sep)\n");
	printf("  --verbose           Show more detailed output\n");
	printf("  -h, --help          Show this help message\n");
}

__attribute__((cold)) void parse_cli(LocConfig* cfg, int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recurse") == 0) {
			cfg->recurse = true;
		} else if (strcmp(argv[i], "-f") == 0 ||
		 strcmp(argv[i], "--files") == 0) {
			cfg->show_files = true;
		} else if (strcmp(argv[i], "-h") == 0 ||
		 strcmp(argv[i], "--help") == 0) {
			cfg->help = true;
		} else if (strcmp(argv[i], "--list-unknown") == 0) {
			cfg->list_unknown = true;
		} else if (
		 (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--load") == 0) &&
		 i + 1 < argc) {
			cfg->lang_load_path = argv[++i];
		} else if (
		 (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--append") == 0) &&
		 i + 1 < argc) {
			cfg->lang_append_path = argv[++i];
		} else if (strcmp(argv[i], "--verbose") == 0) {
			cfg->verbose = true;
		} else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
			cfg->filter = argv[++i];
		} else if (strcmp(argv[i], "-s") == 0 ||
		 strcmp(argv[i], "--sort") == 0) {
			i++;
			if (i < argc) {
				if (strcmp(argv[i], "code") == 0) {
					cfg->sort_order = LOC_SORT_CODE;
				} else if (strcmp(argv[i], "comment") == 0) {
					cfg->sort_order = LOC_SORT_COMMENT;
				} else if (strcmp(argv[i], "blank") == 0) {
					cfg->sort_order = LOC_SORT_BLANK;
				} else if (strcmp(argv[i], "files") == 0) {
					cfg->sort_order = LOC_SORT_FILES;
				} else {
					cfg->sort_order = LOC_SORT_TOTAL;
				}
			}
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
