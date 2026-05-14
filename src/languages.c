#include "include/languages.h"

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

static const char* json_skip_whitespace(const char* p)
{
	while (p && *p && isspace((unsigned char) *p)) {
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
	bool escaped = false;

	while (*p) {
		if (!escaped && *p == '"') {
			break;
		}

		if (i < len - 1) {
			buf[i++] = *p;
		}

		escaped = (!escaped && *p == '\\');

		p++;
	}

	if (!*p) {
		return NULL;
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
		char tmp[2];

		return json_read_string(p, tmp, sizeof(tmp));

	} else if (*p == '[' || *p == '{') {
		char open = *p;
		char close = (open == '[') ? ']' : '}';

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
		while (*p && !strchr(",]} \n\r\t", *p)) {
			p++;
		}
	}

	return p;
}

static const char* parse_extensions(const char* p, Language* lang)
{
	if (*p != '[') {
		return p;
	}

	p++;

	while (*p && *p != ']') {
		p = json_skip_whitespace(p);

		if (lang->n_extensions >= MAX_EXTENSIONS) {
			p = json_skip_value(p);
		} else {
			p = json_read_string(p, lang->extensions[lang->n_extensions],
			 MAX_EXT_LEN);
			if (!p) {
				break;
			}

			lang->n_extensions++;
		}

		p = json_skip_whitespace(p);

		if (*p == ',') {
			p++;
		}
	}

	if (*p == ']') {
		p++;
	}

	return p;
}

static const char* parse_line_comments(const char* p, Language* lang)
{
	if (*p != '[') {
		return p;
	}

	p++;

	while (*p && *p != ']') {
		p = json_skip_whitespace(p);

		if (lang->n_line_comments >= MAX_LINE_COMMENTS) {
			p = json_skip_value(p);
		} else {
			LineCommentRule* lc = &lang->line_comments[lang->n_line_comments];

			memset(lc, 0, sizeof(*lc));

			p = json_read_string(p, lc->start, MAX_COMMENT_LEN);

			lc->len = (uint8_t) strlen(lc->start);

			lang->n_line_comments++;
		}

		p = json_skip_whitespace(p);

		if (*p == ',') {
			p++;
		}
	}

	if (*p == ']') {
		p++;
	}

	return p;
}

static const char* parse_multi_line(const char* p, Language* lang)
{
	if (*p != '[') {
		return p;
	}

	p++;

	while (*p && *p != ']') {
		p = json_skip_whitespace(p);

		if (*p != '[') {
			p = json_skip_value(p);
			continue;
		}

		p++;

		if (lang->n_multi_line >= MAX_BLOCK_COMMENTS) {
			p = json_skip_value(p);
		} else {
			MultiLineRule* ml = &lang->multi_line[lang->n_multi_line];

			memset(ml, 0, sizeof(*ml));

			p = json_read_string(p, ml->start, MAX_COMMENT_LEN);

			ml->start_len = (uint8_t) strlen(ml->start);

			p = json_skip_whitespace(p);

			if (*p == ',') {
				p++;
			}

			p = json_read_string(p, ml->end, MAX_COMMENT_LEN);

			ml->end_len = (uint8_t) strlen(ml->end);

			/*
			 * Default false for now.
			 * Can later load from JSON.
			 */
			ml->nested = false;

			lang->n_multi_line++;
		}

		p = json_skip_whitespace(p);

		if (*p == ']') {
			p++;
		}

		p = json_skip_whitespace(p);

		if (*p == ',') {
			p++;
		}
	}

	if (*p == ']') {
		p++;
	}

	return p;
}

static const char* parse_quotes(const char* p, Language* lang)
{
	if (*p != '[') {
		return p;
	}

	p++;

	while (*p && *p != ']') {
		p = json_skip_whitespace(p);

		if (*p != '{') {
			p = json_skip_value(p);
			continue;
		}

		p++;

		if (lang->n_quotes >= MAX_QUOTES) {
			p = json_skip_value(p);
		} else {
			QuoteRule* q = &lang->quotes[lang->n_quotes];

			memset(q, 0, sizeof(*q));

			while (*p && *p != '}') {
				char key[32];

				p = json_read_string(p, key, sizeof(key));
				if (!p) {
					break;
				}

				p = json_skip_whitespace(p);

				if (*p == ':') {
					p++;
				}

				p = json_skip_whitespace(p);

				switch (key[0]) {
				case 's':

					if (strcmp(key, "start") == 0) {
						p = json_read_string(p, q->start, sizeof(q->start));

						q->start_len = (uint8_t) strlen(q->start);

					} else {
						p = json_skip_value(p);
					}

					break;

				case 'e':

					if (strcmp(key, "end") == 0) {
						p = json_read_string(p, q->end, sizeof(q->end));

						q->end_len = (uint8_t) strlen(q->end);

					} else {
						p = json_skip_value(p);
					}

					break;

				default:
					p = json_skip_value(p);
					break;
				}

				p = json_skip_whitespace(p);

				if (*p == ',') {
					p++;
				}
			}

			/*
			 * Assume C-style escaping for now.
			 */
			q->escape = true;
			q->multiline = false;

			lang->n_quotes++;
		}

		if (*p == '}') {
			p++;
		}

		p = json_skip_whitespace(p);

		if (*p == ',') {
			p++;
		}
	}

	if (*p == ']') {
		p++;
	}

	return p;
}

void load_languages(const unsigned char* data, size_t len, bool append)
{
	(void) len;

	if (!append) {
		g_n_langs = 0;
	}

	if (!data) {
		return;
	}

	const char* p = (const char*) data;

	p = json_skip_whitespace(p);

	/*
	 * New SCC format:
	 *
	 * {
	 *   "C": { ... },
	 *   "Rust": { ... }
	 * }
	 */
	if (!p || *p != '{') {
		return;
	}

	p++;

	while (p && *p && *p != '}' && g_n_langs < MAX_LANGS) {
		char lang_name[MAX_LANG_NAME_LEN];

		p = json_skip_whitespace(p);

		p = json_read_string(p, lang_name, sizeof(lang_name));

		if (!p) {
			break;
		}

		p = json_skip_whitespace(p);

		if (*p != ':') {
			break;
		}

		p++;

		p = json_skip_whitespace(p);

		if (*p != '{') {
			break;
		}

		p++;

		Language temp;

		memset(&temp, 0, sizeof(Language));

		printf("DEBUG: Parsed %s\n", lang_name);
		strncpy(temp.name, lang_name, MAX_LANG_NAME_LEN - 1);

		while (p && *p && *p != '}') {
			char key[64];

			p = json_read_string(p, key, sizeof(key));
			if (!p) {
				break;
			}

			p = json_skip_whitespace(p);

			if (*p == ':') {
				p++;
			}

			p = json_skip_whitespace(p);

			switch (key[0]) {
			case 'e':

				if (strcmp(key, "extensions") == 0) {
					p = parse_extensions(p, &temp);
				} else {
					p = json_skip_value(p);
				}

				break;

			case 'l':

				if (strcmp(key, "line_comment") == 0) {
					p = parse_line_comments(p, &temp);
				} else {
					p = json_skip_value(p);
				}

				break;

			case 'm':

				if (strcmp(key, "multi_line") == 0) {
					p = parse_multi_line(p, &temp);
				} else {
					p = json_skip_value(p);
				}

				break;

			case 'q':

				if (strcmp(key, "quotes") == 0) {
					p = parse_quotes(p, &temp);
				} else {
					p = json_skip_value(p);
				}

				break;

			case 'd':

				if (strcmp(key, "data_only") == 0) {
					if (strncmp(p, "true", 4) == 0) {
						temp.data_only = true;
						p += 4;
					} else if (strncmp(p, "false", 5) == 0) {
						temp.data_only = false;
						p += 5;
					}

				} else {
					p = json_skip_value(p);
				}

				break;

			default:
				p = json_skip_value(p);
				break;
			}

			p = json_skip_whitespace(p);

			if (*p == ',') {
				p++;
			}
		}

		if (p && *p == '}') {
			p++;
		}

		g_langs[g_n_langs++] = temp;

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
	(void) ext;
	return false;
}
