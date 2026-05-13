#ifndef LOC_LANGUAGES_H
#define LOC_LANGUAGES_H

#include <stdbool.h>
#include <stddef.h>

#include "set.h"
#include "types.h"

extern Language g_langs[MAX_LANGS];
extern int g_n_langs;

void load_languages(const unsigned char* data, size_t len, bool append);
void build_lookup_table(void);
int find_language(LangLookupParams params);
bool is_ignored_extension(const char* ext);

#endif
