#include "include/cli.h"

#include <stdio.h>
#include <string.h>

#include "include/minicli.h"

__attribute__((cold)) void loc_config_init(LocConfig* cfg)
{
    memset(cfg, 0, sizeof(LocConfig));
    cfg->output_fmt = LOC_FMT_TERMINAL;
    cfg->sort_order = LOC_SORT_TOTAL;
}

static int cb_recurse(int argc, char** argv, void* user_data)
{
    (void) argc;
    (void) argv;
    ((LocConfig*) user_data)->recurse = true;
    return 0;
}

static int cb_files(int argc, char** argv, void* user_data)
{
    (void) argc;
    (void) argv;
    ((LocConfig*) user_data)->show_files = true;
    return 0;
}

static int cb_list_unknown(int argc, char** argv, void* user_data)
{
    (void) argc;
    (void) argv;
    ((LocConfig*) user_data)->list_unknown = true;
    return 0;
}

static int cb_verbose(int argc, char** argv, void* user_data)
{
    (void) argc;
    (void) argv;
    ((LocConfig*) user_data)->verbose = true;
    return 0;
}

static int cb_load(int argc, char** argv, void* user_data)
{
    if (argc > 0) {
        ((LocConfig*) user_data)->lang_load_path = argv[0];
        return 1;
    }
    return 0;
}

static int cb_append(int argc, char** argv, void* user_data)
{
    if (argc > 0) {
        ((LocConfig*) user_data)->lang_append_path = argv[0];
        return 1;
    }
    return 0;
}

static int cb_filter(int argc, char** argv, void* user_data)
{
    if (argc > 0) {
        ((LocConfig*) user_data)->filter = argv[0];
        return 1;
    }
    return 0;
}

static int cb_sort(int argc, char** argv, void* user_data)
{
    if (argc > 0) {
        LocConfig* cfg = (LocConfig*) user_data;
        if (strcmp(argv[0], "code") == 0) {
            cfg->sort_order = LOC_SORT_CODE;
        } else if (strcmp(argv[0], "comment") == 0) {
            cfg->sort_order = LOC_SORT_COMMENT;
        } else if (strcmp(argv[0], "blank") == 0) {
            cfg->sort_order = LOC_SORT_BLANK;
        } else if (strcmp(argv[0], "files") == 0) {
            cfg->sort_order = LOC_SORT_FILES;
        } else {
            cfg->sort_order = LOC_SORT_TOTAL;
        }
        return 1;
    }
    return 0;
}

static int cb_output(int argc, char** argv, void* user_data)
{
    if (argc > 0) {
        LocConfig* cfg = (LocConfig*) user_data;
        if (strcmp(argv[0], "json") == 0) {
            cfg->output_fmt = LOC_FMT_JSON;
        } else if (strcmp(argv[0], "html") == 0) {
            cfg->output_fmt = LOC_FMT_HTML;
        } else if (strcmp(argv[0], "sql") == 0) {
            cfg->output_fmt = LOC_FMT_SQL;
        } else {
            cfg->output_fmt = LOC_FMT_TERMINAL;
        }
        return 1;
    }
    return 0;
}

__attribute__((cold)) void parse_cli(LocConfig* cfg, int argc, char** argv)
{
    CliParser parser;
    cli_init(&parser,
     (CliInitParams) {"mini-loc", "A fast lines-of-code counter"});

    cli_add_argument(&parser,
     (CliArgument) {
         "--recurse", "-r", "Recurse into directories", cb_recurse, cfg});
    cli_add_argument(&parser,
     (CliArgument) {"--files", "-f", "Show per-file results", cb_files, cfg});
    cli_add_argument(&parser,
     (CliArgument) {"--sort", "-s",
         "Sort by: total, code, comment, blank, files", cb_sort, cfg});
    cli_add_argument(&parser,
     (CliArgument) {"--output", "-o",
         "Output format: terminal, json, html, sql", cb_output, cfg});
    cli_add_argument(&parser,
     (CliArgument) {
         "--load", "-l", "Load custom language definitions", cb_load, cfg});
    cli_add_argument(&parser,
     (CliArgument) {"--append", "-a", "Append custom language definitions",
         cb_append, cfg});
    cli_add_argument(&parser,
     (CliArgument) {"--list-unknown", NULL,
         "List files with unknown extensions", cb_list_unknown, cfg});
    cli_add_argument(&parser,
     (CliArgument) {"--filter", NULL,
         "Only process these extensions (comma-sep)", cb_filter, cfg});
    cli_add_argument(&parser,
     (CliArgument) {
         "--verbose", NULL, "Show more detailed output", cb_verbose, cfg});

    cli_parse(&parser, argc, argv);

    cli_destroy(&parser);
}
