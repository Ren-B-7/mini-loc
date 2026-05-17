#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compiler attribute compat */
#if defined(__GNUC__) || defined(__clang__)
#define COLD_ATTR __attribute__((cold))
#else
#define COLD_ATTR
#endif

#include "include/cli.h"
#include "include/count.h"
#include "include/fs.h"
#include "include/languages.h"
#include "include/languages_data.h"
#include "include/output.h"
#include "include/types.h"

static FileResult* g_files = NULL;
static int g_n_files = 0;
static int g_capacity = 0;
static LocConfig g_cfg;

static void process_file_cb(const char* path, size_t size, void* user)
{
    (void) user;
    const char* ext = strrchr(path, '.');
    g_cfg.total_bytes += size;
    if (g_n_files >= g_capacity) {
        g_capacity = g_capacity == 0 ? 1024 : g_capacity * 2;
        FileResult* temp = (FileResult*) realloc(g_files,
         sizeof(FileResult) * (size_t) g_capacity);
        if (!temp) {
            return;
        }
        g_files = temp;
    }
    int li = find_language((LangLookupParams) {path, ext});
    if (li == -1) {
        li = find_language((LangLookupParams) {path, NULL});
    }
    if (li == -1 && !g_cfg.list_unknown) {
        return;
    }
    FileResult* fr = &g_files[g_n_files++];
    fr->path = g_cfg.show_files ? strdup(path) : NULL;
    fr->ext = (g_cfg.show_files && ext) ? strdup(ext) : NULL;
    fr->lang_idx = li;
    fr->counts = count_file(path, li);
}

COLD_ATTR int main(int argc, char** argv)
{
    loc_config_init(&g_cfg);
    parse_cli(&g_cfg, argc, argv);

    load_languages();

    if (g_cfg.lang_load_path) {
        load_languages_from_file(g_cfg.lang_load_path, false);
    }
    if (g_cfg.lang_append_path) {
        load_languages_from_file(g_cfg.lang_append_path, true);
    }

    build_lookup_table();
    bool any_path = false;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--load") == 0 ||
             strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--append") == 0 ||
             strcmp(argv[i], "--filter") == 0 || strcmp(argv[i], "-o") == 0 ||
             strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-s") == 0 ||
             strcmp(argv[i], "--sort") == 0) {
                i++;
            }
            continue;
        }
        process_path(argv[i], g_cfg.recurse, process_file_cb, NULL);
        any_path = true;
    }
    if (!any_path) {
        process_path(".", g_cfg.recurse, process_file_cb, NULL);
    }
    loc_print_report(g_cfg.output_fmt, g_files, g_n_files, g_langs, g_n_langs,
     (LocOutputParams) {g_cfg.show_files, g_cfg.verbose, g_cfg.no_bytes,
         g_cfg.total_bytes, g_cfg.sort_order});
    if (g_files) {
        for (int i = 0; i < g_n_files; i++) {
            free(g_files[i].path);
        }
        free(g_files);
    }
    return 0;
}
