#include "include/languages.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "include/languages_data.h"
#include "include/minicli.h"
#include "include/set.h"
#include "include/types.h"

Language g_langs[MAX_LANGS];
int g_n_langs = 0;

static ExtEntry g_ext_table[MAX_LANGS * MAX_EXTENSIONS];
static int g_n_ext_entries = 0;

static SimpleSet g_ignored_set;
static bool g_ignored_set_ready = false;

static const char* json_skip_whitespace(const char* p)
{
	if (!p) {
		return NULL;
	}
	while (*p && isspace((unsigned char) *p)) {
		p++;
	}
	return p;
}

static const char* json_read_string(const char* p, char* buf, size_t len)
{
	p = json_skip_whitespace(p);
	if (!p || *p != '"') {
		return NULL;
	}
	p++;
	size_t i = 0;
	while (*p && *p != '"' && i < len - 1) {
		if (*p == '\\') {
			p++;
		}
		buf[i++] = *p++;
	}
	buf[i] = '\0';
	if (*p == '"') {
		p++;
	}
	return p;
}

static const char* json_skip_value(const char* p)
{
	p = json_skip_whitespace(p);
	if (!p) {
		return NULL;
	}
	if (*p == '"') {
		p++;
		while (*p && (*p != '"' || *(p - 1) == '\\')) {
			p++;
		}
		if (*p == '"') {
			p++;
		}
	} else if (*p == '[' || *p == '{') {
		char open = *p, close = (open == '[') ? ']' : '}';
		int depth = 1;
		p++;
		while (*p && depth > 0) {
			if (*p == open) {
				depth++;
			} else if (*p == close) {
				depth--;
			}
			p++;
		}
	} else {
		while (*p && !strchr(",] \n\r\t}", *p)) {
			p++;
		}
	}
	return p;
}

void load_languages(const unsigned char* data, size_t len, bool append)
{
	(void) len;
	if (!append) {
		g_n_langs = 0;
		if (g_ignored_set_ready) {
			set_destroy(&g_ignored_set);
			g_ignored_set_ready = false;
		}
		set_init(&g_ignored_set);
		g_ignored_set_ready = true;
	}
	if (!data) {
		return;
	}
	const char* p = (const char*) data;
	p = json_skip_whitespace(p);
	if (!p || *p != '[') {
		return;
	}
	p++;

	while (p && *p && *p != ']' && g_n_langs < MAX_LANGS) {
		p = json_skip_whitespace(p);
		if (p && *p == '{') {
			p++;
			char key[64];
			Language temp;
			memset(&temp, 0, sizeof(Language));
			bool is_config = false;
			while (p && *p && *p != '}') {
				p = json_read_string(p, key, sizeof(key));
				p = json_skip_whitespace(p);
				if (p && *p == ':') {
					p++;
				}
				p = json_skip_whitespace(p);
				if (strcmp(key, "name") == 0) {
					char name[MAX_LANG_NAME_LEN];
					p = json_read_string(p, name, sizeof(name));
					if (strcmp(name, "config") == 0) {
						is_config = true;
					} else {
						memcpy(temp.name, name, MAX_LANG_NAME_LEN);
					}
				} else if (is_config &&
				 strcmp(key, "ignored_extensions") == 0) {
					if (p && *p == '[') {
						p++;
						while (p && *p && *p != ']') {
							char ign_ext[16];
							p = json_read_string(p, ign_ext, sizeof(ign_ext));
							set_add_str(&g_ignored_set, ign_ext);
							p = json_skip_whitespace(p);
							if (p && *p == ',') {
								p++;
							}
							p = json_skip_whitespace(p);
						}
						if (p && *p == ']') {
							p++;
						}
					}
				} else if (!is_config) {
					if (strcmp(key, "extensions") == 0) {
						if (p && *p == '[') {
							p++;
							while (p && *p && *p != ']') {
								p = json_read_string(p,
								 temp.extensions[temp.n_extensions++],
								 MAX_EXT_LEN);
								p = json_skip_whitespace(p);
								if (p && *p == ',') {
									p++;
								}
								p = json_skip_whitespace(p);
							}
							if (p && *p == ']') {
								p++;
							}
						}
					} else if (strcmp(key, "line_comments") == 0) {
						if (p && *p == '[') {
							p++;
							while (p && *p && *p != ']') {
								p = json_read_string(p,
								 temp.line_comments[temp.n_line_comments++],
								 MAX_COMMENT_LEN);
								p = json_skip_whitespace(p);
								if (p && *p == ',') {
									p++;
								}
								p = json_skip_whitespace(p);
							}
							if (p && *p == ']') {
								p++;
							}
						}
					} else if (strcmp(key, "block_comments") == 0) {
						if (p && *p == '[') {
							p++;
							while (p && *p && *p != ']') {
								if (*p == '[') {
									p++;
									p = json_read_string(p,
									 temp.block_start[temp.n_block_comments],
									 MAX_COMMENT_LEN);
									p = json_skip_whitespace(p);
									if (p && *p == ',') {
										p++;
									}
									p = json_read_string(p,
									 temp.block_end[temp.n_block_comments++],
									 MAX_COMMENT_LEN);
									p = json_skip_whitespace(p);
									if (p && *p == ']') {
										p++;
									}
								}
								p = json_skip_whitespace(p);
								if (p && *p == ',') {
									p++;
								}
								p = json_skip_whitespace(p);
							}
							if (p && *p == ']') {
								p++;
							}
						}
					} else if (strcmp(key, "data_only") == 0) {
						if (p && strncmp(p, "true", 4) == 0) {
							temp.data_only = true;
							p += 4;
						} else if (p && strncmp(p, "false", 5) == 0) {
							temp.data_only = false;
							p += 5;
						}
					} else {
						p = json_skip_value(p);
					}
				} else {
					p = json_skip_value(p);
				}
				p = json_skip_whitespace(p);
				if (p && *p == ',') {
					p++;
				}
				p = json_skip_whitespace(p);
			}
			if (p && *p == '}') {
				p++;
			}
			if (!is_config) {
				for (int i = 0; i < temp.n_line_comments; i++) {
					temp.line_comment_lens[i] =
					 (uint8_t) strlen(temp.line_comments[i]);
				}
				for (int i = 0; i < temp.n_block_comments; i++) {
					temp.block_start_lens[i] =
					 (uint8_t) strlen(temp.block_start[i]);
					temp.block_end_lens[i] =
					 (uint8_t) strlen(temp.block_end[i]);
				}
				g_langs[g_n_langs++] = temp;
			}
		}
		p = json_skip_whitespace(p);
		if (p && *p == ',') {
			p++;
		}
	}
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
	if (!ext) {
		return false;
	}
	return set_contains_str(&g_ignored_set, ext) == SET_TRUE;
}
