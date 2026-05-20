#ifndef LOC_LANGUAGES_H
#define LOC_LANGUAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "set.h"
#include "types.h"

/*
 * Global language database loaded from the compiled
 * languages_data blob.
 */
extern Language g_langs[MAX_LANGS];
extern int g_n_langs;
extern uint32_t g_complexity_starts[MAX_LANGS][8];

/*
 * Load language definitions from embedded static data.
 */
void load_languages(void);

/*
 * Load language definitions from an external JSON file.
 *
 * append == false:
 *     replaces all currently loaded languages (including built-ins)
 *
 * append == true:
 *     appends to existing language table
 */
void load_languages_from_file(const char* path, bool append);

/*
 * Builds the extension -> language lookup table.
 *
 * Must be called after load_languages().
 */
void build_lookup_table(void);

/*
 * Lookup a language index by extension or filename.
 *
 * If params.ext is NULL:
 *     falls back to filename matching
 *
 * Returns:
 *     >= 0 language index
 *     -1 on failure
 */
int find_language(LangLookupParams params);

#endif /* LOC_LANGUAGES_H */
