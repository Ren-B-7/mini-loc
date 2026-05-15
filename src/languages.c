#include "include/languages.h"

#include <cjson/cJSON.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "include/languages_data.h"
#include "include/types.h"

Language g_langs[MAX_LANGS];
int g_n_langs = 0;

static ExtEntry g_ext_table[MAX_LANGS * MAX_EXTENSIONS];
static int g_n_ext_entries = 0;

void load_languages(void)
{
    g_n_langs = 0;

    for (int i = 0; i < g_n_langs_data && i < MAX_LANGS; i++) {
        g_langs[g_n_langs++] = g_langs_data[i];
    }
}

void load_languages_from_file(const char* path, bool append)
{
    if (!append) {
        g_n_langs = 0;
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        return;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    long len = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return;
    }

    if (len <= 0 || len >= MAX_FILE_SIZE) {
        fclose(f);
        return;
    }

    size_t file_size = (size_t) len;

    /* calloc zero-initialises the buffer, so the null terminator at
       data[file_size] is already in place before fread runs. This avoids
       any indexed write using a tainted value derived from fread/ftell,
       which the analyser cannot reason about safely. */
    char* data = calloc(file_size + 1, 1);
    if (!data) {
        fclose(f);
        return;
    }

    size_t n_read = fread(data, 1, file_size, f);
    if (n_read != file_size) {
        free(data);
        fclose(f);
        return;
    }
    fclose(f);

    cJSON* root = cJSON_Parse(data);
    free(data);

    if (!root) {
        return;
    }

    cJSON* lang_node = NULL;
    cJSON_ArrayForEach(lang_node, root)
    {
        if (g_n_langs >= MAX_LANGS) {
            break;
        }

        Language* l = &g_langs[g_n_langs++];
        memset(l, 0, sizeof(Language));

        if (lang_node->string) {
            strncpy(l->name, lang_node->string, MAX_LANG_NAME_LEN - 1);
            l->name[MAX_LANG_NAME_LEN - 1] = '\0';
        }

        cJSON* data_only = cJSON_GetObjectItem(lang_node, "data_only");
        if (cJSON_IsBool(data_only)) {
            l->data_only = cJSON_IsTrue(data_only);
        }

        cJSON* extensions = cJSON_GetObjectItem(lang_node, "extensions");
        if (cJSON_IsArray(extensions)) {
            cJSON* ext = NULL;
            cJSON_ArrayForEach(ext, extensions)
            {
                if (l->n_extensions >= MAX_EXTENSIONS) {
                    break;
                }
                if (cJSON_IsString(ext)) {
                    strncpy(l->extensions[l->n_extensions], ext->valuestring,
                     MAX_EXT_LEN - 1);
                    l->extensions[l->n_extensions][MAX_EXT_LEN - 1] = '\0';
                    l->n_extensions++;
                }
            }
        }

        cJSON* line_comments = cJSON_GetObjectItem(lang_node, "line_comment");
        if (cJSON_IsArray(line_comments)) {
            cJSON* lc = NULL;
            cJSON_ArrayForEach(lc, line_comments)
            {
                if (l->n_line_comments >= MAX_LINE_COMMENTS) {
                    break;
                }
                if (cJSON_IsString(lc)) {
                    LineCommentRule* r =
                     &l->line_comments[l->n_line_comments++];
                    strncpy(r->start, lc->valuestring, MAX_COMMENT_LEN - 1);
                    r->start[MAX_COMMENT_LEN - 1] = '\0';
                    r->len = (uint8_t) strlen(r->start);
                }
            }
        }

        cJSON* multi_line = cJSON_GetObjectItem(lang_node, "multi_line");
        if (cJSON_IsArray(multi_line)) {
            cJSON* ml = NULL;
            cJSON_ArrayForEach(ml, multi_line)
            {
                if (l->n_multi_line >= MAX_BLOCK_COMMENTS) {
                    break;
                }
                if (cJSON_IsArray(ml) && cJSON_GetArraySize(ml) == 2) {
                    MultiLineRule* r = &l->multi_line[l->n_multi_line++];
                    cJSON* start = cJSON_GetArrayItem(ml, 0);
                    cJSON* end = cJSON_GetArrayItem(ml, 1);
                    if (cJSON_IsString(start) && cJSON_IsString(end)) {
                        strncpy(r->start, start->valuestring,
                         MAX_COMMENT_LEN - 1);
                        r->start[MAX_COMMENT_LEN - 1] = '\0';
                        r->start_len = (uint8_t) strlen(r->start);
                        strncpy(r->end, end->valuestring, MAX_COMMENT_LEN - 1);
                        r->end[MAX_COMMENT_LEN - 1] = '\0';
                        r->end_len = (uint8_t) strlen(r->end);
                        r->nested = false;
                    }
                }
            }
        }

        cJSON* quotes = cJSON_GetObjectItem(lang_node, "quotes");
        if (cJSON_IsArray(quotes)) {
            cJSON* q = NULL;
            cJSON_ArrayForEach(q, quotes)
            {
                if (l->n_quotes >= MAX_QUOTES) {
                    break;
                }
                if (cJSON_IsObject(q)) {
                    QuoteRule* r = &l->quotes[l->n_quotes++];
                    cJSON* start = cJSON_GetObjectItem(q, "start");
                    cJSON* end = cJSON_GetObjectItem(q, "end");
                    if (cJSON_IsString(start)) {
                        strncpy(r->start, start->valuestring, 7);
                        r->start[7] = '\0';
                        r->start_len = (uint8_t) strlen(r->start);
                    }
                    if (cJSON_IsString(end)) {
                        strncpy(r->end, end->valuestring, 7);
                        r->end[7] = '\0';
                        r->end_len = (uint8_t) strlen(r->end);
                    }
                    r->escape = true;
                }
            }
        }

        cJSON* complexity = cJSON_GetObjectItem(lang_node, "complexitychecks");
        if (cJSON_IsArray(complexity)) {
            cJSON* cx = NULL;
            cJSON_ArrayForEach(cx, complexity)
            {
                if (l->n_complexity >= MAX_COMPLEXITY_CHECKS) {
                    break;
                }
                if (cJSON_IsString(cx)) {
                    ComplexityRule* r = &l->complexity[l->n_complexity++];
                    strncpy(r->token, cx->valuestring, MAX_COMPLEXITY_LEN - 1);
                    r->token[MAX_COMPLEXITY_LEN - 1] = '\0';
                    r->len = (uint8_t) strlen(r->token);
                }
            }
        }
    }

    cJSON_Delete(root);
}

static __attribute__((cold)) int ext_entry_cmp(const void* a, const void* b)
{
    return strcasecmp(((const ExtEntry*) a)->ext, ((const ExtEntry*) b)->ext);
}

static __attribute__((cold)) int ext_cmp_str(const void* key, const void* entry)
{
    return strcasecmp((const char*) key, ((const ExtEntry*) entry)->ext);
}

__attribute__((cold)) void build_lookup_table(void)
{
    g_n_ext_entries = 0;

    for (int i = 0; i < g_n_langs; i++) {
        for (int j = 0; j < g_langs[i].n_extensions; j++) {
            if (g_n_ext_entries >= MAX_LANGS * MAX_EXTENSIONS) {
                break;
            }

            ExtEntry* e = &g_ext_table[g_n_ext_entries++];

            strncpy(e->ext, g_langs[i].extensions[j], MAX_EXT_LEN - 1);

            e->ext[MAX_EXT_LEN - 1] = '\0';

            e->lang_idx = i;
        }
    }

    qsort(g_ext_table, (size_t) g_n_ext_entries, sizeof(ExtEntry),
     ext_entry_cmp);
}

int find_language(LangLookupParams params)
{
    const char* search_key;
    const char* base = NULL;

    if (!params.ext) {
        base = strrchr(params.path, '/');

        search_key = base ? base + 1 : params.path;

    } else {
        search_key = params.ext + 1;
    }

    const ExtEntry* found = (const ExtEntry*) bsearch(search_key, g_ext_table,
     (size_t) g_n_ext_entries, sizeof(ExtEntry), ext_cmp_str);

    return found ? found->lang_idx : -1;
}

__attribute__((cold)) bool is_ignored_extension(const char* ext)
{
    (void) ext;
    return false;
}
