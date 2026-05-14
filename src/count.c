#include "include/count.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/languages.h"
#include "include/types.h"

typedef enum {
	CHAR_NONE = 0,
	CHAR_NEWLINE = 1 << 0,
	CHAR_WHITESPACE = 1 << 1,
	CHAR_SLASH = 1 << 2,
	CHAR_STAR = 1 << 3,
	CHAR_QUOTE = 1 << 4,
	CHAR_BACKSLASH = 1 << 5,
} CharFlags;

static const uint8_t char_table[256] = {
    [' '] = CHAR_WHITESPACE,
    ['\t'] = CHAR_WHITESPACE,
    ['\r'] = CHAR_WHITESPACE,
    ['\v'] = CHAR_WHITESPACE,
    ['\f'] = CHAR_WHITESPACE,
    ['\n'] = CHAR_NEWLINE,

    ['/'] = CHAR_SLASH,
    ['*'] = CHAR_STAR,

    ['"'] = CHAR_QUOTE,
    ['\''] = CHAR_QUOTE,

    ['\\'] = CHAR_BACKSLASH,
};

static inline bool is_whitespace(unsigned char c)
{
	return (char_table[c] & CHAR_WHITESPACE) != 0;
}

static inline bool
match_token(const char* p, const char* end, const char* token, uint8_t len)
{
	if ((size_t) (end - p) < len) {
		return false;
	}

	switch (len) {
	case 1:
		return p[0] == token[0];

	case 2:
		return *(const uint16_t*) p == *(const uint16_t*) token;

	case 4:
		return *(const uint32_t*) p == *(const uint32_t*) token;

	case 8:
		return *(const uint64_t*) p == *(const uint64_t*) token;

	default:
		return memcmp(p, token, len) == 0;
	}
}

static inline void finalize_line(Scanner* s, Counts* c)
{
	if (!s->line_has_code && !s->line_has_comment) {
		c->blank++;
	} else if (s->line_has_code) {
		c->code++;
	} else {
		c->comment++;
	}

	s->line_has_code = false;
	s->line_has_comment = false;
}

Counts count_file(const char* path, int lang_idx)
{
	Counts c = {0};

	FILE* f = fopen(path, "rb");

	if (!f) {
		return c;
	}

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return c;
	}

	long file_len = ftell(f);

	if (file_len < 0) {
		fclose(f);
		return c;
	}

	if (file_len == 0 || file_len >= MAX_FILE_SIZE) {
		fclose(f);
		return c;
	}

	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return c;
	}

	size_t size = (size_t) file_len;

	/*
	 * Prevent overflow on +1
	 */
	if (size >= SIZE_MAX - 1) {
		fclose(f);
		return c;
	}

	size_t alloc_size = size + 1;

	char* buf = calloc(alloc_size, 1);

	if (!buf) {
		fclose(f);
		return c;
	}

	size_t nread = fread(buf, 1, size, f);

	fclose(f);

	if (nread > size) {
		nread = size;
	}

	Language* lang = (lang_idx >= 0) ? &g_langs[lang_idx] : NULL;

	if (!lang) {
		free(buf);
		return c;
	}

	Scanner s = {0};

	const char* p = buf;
	const char* end = buf + nread;

	while (p < end) {
		unsigned char ch = (unsigned char) *p;

		switch (s.state) {
		case SCAN_NORMAL: {
			bool matched = false;

			/*
			 * Fast whitespace path
			 */
			if (is_whitespace(ch)) {
				break;
			}

			/*
			 * Quote handling
			 */
			for (uint16_t i = 0; i < lang->n_quotes; i++) {
				QuoteRule* q = &lang->quotes[i];

				if (q->start[0] != *p) {
					continue;
				}

				if (match_token(p, end, q->start, q->start_len)) {
					s.state = SCAN_STRING;
					s.quote_index = i;
					s.line_has_code = true;

					p += q->start_len - 1;

					matched = true;
					break;
				}
			}

			if (matched) {
				break;
			}

			/*
			 * Line comments
			 */
			for (uint16_t i = 0; i < lang->n_line_comments; i++) {
				LineCommentRule* lc = &lang->line_comments[i];

				if (lc->start[0] != *p) {
					continue;
				}

				if (match_token(p, end, lc->start, lc->len)) {
					s.state = SCAN_LINE_COMMENT;
					s.line_has_comment = true;

					p += lc->len - 1;

					matched = true;
					break;
				}
			}

			if (matched) {
				break;
			}

			/*
			 * Block comments
			 */
			for (uint16_t i = 0; i < lang->n_multi_line; i++) {
				MultiLineRule* ml = &lang->multi_line[i];

				if (ml->start[0] != *p) {
					continue;
				}

				if (match_token(p, end, ml->start, ml->start_len)) {
					s.state = SCAN_BLOCK_COMMENT;
					s.block_index = i;
					s.block_depth = 1;
					s.line_has_comment = true;

					p += ml->start_len - 1;

					matched = true;
					break;
				}
			}

			if (matched) {
				break;
			}

			/*
			 * Everything else is code
			 */
			s.line_has_code = true;

			break;
		}

		case SCAN_LINE_COMMENT:

			s.line_has_comment = true;

			break;

		case SCAN_BLOCK_COMMENT: {
			s.line_has_comment = true;

			MultiLineRule* ml = &lang->multi_line[s.block_index];

			/*
			 * Nested comments
			 */
			if (ml->nested && *p == ml->start[0] &&
			 match_token(p, end, ml->start, ml->start_len)) {
				s.block_depth++;

				p += ml->start_len - 1;

				break;
			}

			/*
			 * Comment end
			 */
			if (*p == ml->end[0] && match_token(p, end, ml->end, ml->end_len)) {
				s.block_depth--;

				p += ml->end_len - 1;

				if (s.block_depth == 0) {
					s.state = SCAN_NORMAL;
				}
			}

			break;
		}

		case SCAN_STRING: {
			QuoteRule* q = &lang->quotes[s.quote_index];

			s.line_has_code = true;

			/*
			 * Escape sequence
			 */
			if (q->escape && (char_table[ch] & CHAR_BACKSLASH) && p + 1 < end) {
				p++;

				break;
			}

			/*
			 * String end
			 */
			if (*p == q->end[0] && match_token(p, end, q->end, q->end_len)) {
				p += q->end_len - 1;

				s.state = SCAN_NORMAL;
			}

			break;
		}
		}

		/*
		 * Newline handling
		 */
		if (char_table[ch] & CHAR_NEWLINE) {
			finalize_line(&s, &c);

			if (s.state == SCAN_LINE_COMMENT) {
				s.state = SCAN_NORMAL;
			}
		}

		p++;
	}

	/*
	 * Final line without trailing newline
	 */
	if (s.line_has_code || s.line_has_comment) {
		finalize_line(&s, &c);
	}

	free(buf);

	return c;
}
