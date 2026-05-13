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

/*
 * Load language definitions from embedded JSON.
 *
 * append == false:
 *     resets all currently loaded languages
 *
 * append == true:
 *     appends to existing language table
 */
void load_languages(const unsigned char* data, size_t len, bool append);

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

/*
 * Returns true if the extension should be skipped.
 *
 * Used for:
 * - binaries
 * - archives
 * - media
 * - generated assets
 */
bool is_ignored_extension(const char* ext);

#endif /* LOC_LANGUAGES_H */
