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
    CHAR_NORMAL = 0,
    CHAR_SPACE  = 1 << 0,
    CHAR_SLASH  = 1 << 1,
    CHAR_STAR   = 1 << 2,
    CHAR_NEWLINE = 1 << 3,
} CharType;

static const uint8_t char_table[256] = {
    [' '] = CHAR_SPACE,
    ['\t'] = CHAR_SPACE,
    ['\n'] = CHAR_NEWLINE,
    ['\r'] = CHAR_SPACE,
    ['\v'] = CHAR_SPACE,
    ['\f'] = CHAR_SPACE,
    ['/'] = CHAR_SLASH,
    ['*'] = CHAR_STAR,
};

bool scan_for_end(const char* p, const char* line_end, const char* end,
 size_t end_len)
{
	if (end_len == 0) {
		return true;
	}
	if (end_len == 1) {
		const char* found = (const char*) memchr(p, (unsigned char) end[0],
		 (size_t) (line_end - p));
		return found != NULL;
	}

	char first = end[0];
	while (p <= line_end - (ptrdiff_t) end_len) {
		ptrdiff_t remaining = line_end - p - (ptrdiff_t) end_len + 1;
		if (remaining <= 0) {
			break;
		}

		const char* found =
		 (const char*) memchr(p, (unsigned char) first, (size_t) remaining);
		if (!found) {
			return false;
		}

		if (end_len == 2) {
			uint16_t v1, v2;
			memcpy(&v1, found, 2);
			memcpy(&v2, end, 2);
			if (v1 == v2) {
				return true;
			}
		} else if (end_len == 4) {
			uint32_t v1, v2;
			memcpy(&v1, found, 4);
			memcpy(&v2, end, 4);
			if (v1 == v2) {
				return true;
			}
		} else {
			if (memcmp(found, end, end_len) == 0) {
				return true;
			}
		}
		p = found + 1;
	}
	return false;
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
	if (file_len < 0 || file_len >= MAX_FILE_SIZE) {
		fclose(f);
		return c;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return c;
	}

	size_t buf_size = (size_t) file_len + 1;
	char* buf = (char*) malloc(buf_size);
	if (!buf) {
		fclose(f);
		return c;
	}
	size_t nread = fread(buf, 1, (size_t) file_len, f);
	fclose(f);
	if (nread >= buf_size) {
		nread = buf_size - 1;
	}

	Language* l = (lang_idx >= 0) ? &g_langs[lang_idx] : NULL;
	bool in_block = false;
	int block_idx = -1;

	char* file_end = buf + nread;
	char* cur = buf;

	while (cur < file_end) {
		char* lf = (char*) memchr(cur, '\n', (size_t) (file_end - cur));
		char* line_end = lf ? lf : file_end;

		if (l == NULL) {
			c.code++;
		} else {
			char* p = cur;
			while (p < line_end && (char_table[(unsigned char)*p] & CHAR_SPACE)) {
				p++;
			}

			if (p == line_end) {
				c.blank++;
			} else {
				bool is_comment = false;
				if (in_block) {
					is_comment = true;
					if (scan_for_end(p, line_end, l->block_end[block_idx],
					     l->block_end_lens[block_idx])) {
						in_block = false;
					}
				} else {
					for (int i = 0; i < l->n_line_comments; i++) {
						size_t clen = l->line_comment_lens[i];
						if ((size_t) (line_end - p) >= clen &&
						 p[0] == l->line_comments[i][0] &&
						 memcmp(p, l->line_comments[i], clen) == 0) {
							is_comment = true;
							break;
						}
					}

					if (!is_comment) {
						for (int i = 0; i < l->n_block_comments; i++) {
							size_t clen = l->block_start_lens[i];
							if ((size_t) (line_end - p) >= clen &&
							 p[0] == l->block_start[i][0] &&
							 memcmp(p, l->block_start[i], clen) == 0) {
								is_comment = true;
								if (!scan_for_end(p + clen, line_end,
								     l->block_end[i], l->block_end_lens[i])) {
									in_block = true;
									block_idx = i;
								}
								break;
							}
						}
					}
				}

				if (is_comment) {
					c.comment++;
				} else {
					c.code++;
				}
			}
		}
		cur = lf ? lf + 1 : file_end;
	}
	free(buf);
	return c;
}
