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

static inline __attribute__((always_inline)) bool is_whitespace(unsigned char c)
{
    return (char_table[c] & CHAR_WHITESPACE) != 0;
}

static inline __attribute__((always_inline)) bool
match_token(const char* p, const char* end, const char* token, uint8_t len)
{
    if ((size_t)(end - p) < len) {
        return false;
    }

    switch (len) {
    case 1:
        return p[0] == token[0];

    case 2: {
        uint16_t v1, v2;
        memcpy(&v1, p, 2);
        memcpy(&v2, token, 2);
        return v1 == v2;
    }

    case 3: {
        uint16_t v1, v2;
        memcpy(&v1, p, 2);
        memcpy(&v2, token, 2);
        return v1 == v2 && p[2] == token[2];
    }

    case 4: {
        uint32_t v1, v2;
        memcpy(&v1, p, 4);
        memcpy(&v2, token, 4);
        return v1 == v2;
    }

    case 8: {
        uint64_t v1, v2;
        memcpy(&v1, p, 8);
        memcpy(&v2, token, 8);
        return v1 == v2;
    }

    default:
        return memcmp(p, token, len) == 0;
    }
}

typedef struct {
    uint8_t quote_idx;
    uint8_t line_comment_idx;
    uint8_t block_comment_idx;
} FirstCharEntry;

static FirstCharEntry g_dispatch[256];

static void build_dispatch_table(Language* lang)
{
    memset(g_dispatch, 0xFF, sizeof(g_dispatch));
    for (int i = 0; i < lang->n_quotes; i++) {
        g_dispatch[(uint8_t)lang->quotes[i].start[0]].quote_idx = (uint8_t)i;
    }
    for (int i = 0; i < lang->n_line_comments; i++) {
        g_dispatch[(uint8_t)lang->line_comments[i].start[0]].line_comment_idx =
         (uint8_t)i;
    }
    for (int i = 0; i < lang->n_multi_line; i++) {
        g_dispatch[(uint8_t)lang->multi_line[i].start[0]].block_comment_idx =
         (uint8_t)i;
    }
}

static inline void finalize_line(Scanner* s, Counts* c)
{
    int has_code = (int)s->line_has_code;
    int has_comment = (int)s->line_has_comment & ~has_code;
    int is_blank = 1 - has_code - has_comment;

    c->code += (uint32_t)has_code;
    c->comment += (uint32_t)has_comment;
    c->blank += (uint32_t)is_blank;

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

    size_t size = (size_t)file_len;

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

    build_dispatch_table(lang);

    Scanner s = {0};

    const char* p = buf;
    const char* end = buf + nread;

    while (p < end) {
        unsigned char ch = (unsigned char)*p;

        if (__builtin_expect(char_table[ch] & CHAR_NEWLINE, 0)) {
            finalize_line(&s, &c);
            if (s.state == SCAN_LINE_COMMENT) {
                s.state = SCAN_NORMAL;
            }
            p++;
            continue;
        }

        switch (s.state) {
        case SCAN_NORMAL: {
            if (is_whitespace(ch)) {
                break;
            }

            FirstCharEntry* e = &g_dispatch[ch];

            if (e->quote_idx != 0xFF) {
                QuoteRule* q = &lang->quotes[e->quote_idx];
                if (match_token(p, end, q->start, q->start_len)) {
                    s.state = SCAN_STRING;
                    s.quote_index = e->quote_idx;
                    s.line_has_code = true;
                    p += q->start_len - 1;
                    break;
                }
            }

            if (e->line_comment_idx != 0xFF) {
                LineCommentRule* lc = &lang->line_comments[e->line_comment_idx];
                if (match_token(p, end, lc->start, lc->len)) {
                    s.state = SCAN_LINE_COMMENT;
                    s.line_has_comment = true;
                    p += lc->len - 1;
                    break;
                }
            }

            if (e->block_comment_idx != 0xFF) {
                MultiLineRule* ml = &lang->multi_line[e->block_comment_idx];
                if (match_token(p, end, ml->start, ml->start_len)) {
                    s.state = ml->nested ?
                     SCAN_BLOCK_COMMENT_NESTED :
                     SCAN_BLOCK_COMMENT;
                    s.block_index = e->block_comment_idx;
                    s.block_depth = 1;
                    s.line_has_comment = true;
                    p += ml->start_len - 1;
                    break;
                }
            }

            s.line_has_code = true;
            break;
        }

        case SCAN_LINE_COMMENT:
            s.line_has_comment = true;
            break;

        case SCAN_BLOCK_COMMENT: {
            s.line_has_comment = true;
            MultiLineRule* ml = &lang->multi_line[s.block_index];

            if (*p == ml->end[0] && match_token(p, end, ml->end, ml->end_len)) {
                p += ml->end_len - 1;
                s.state = SCAN_NORMAL;
            }
            break;
        }

        case SCAN_BLOCK_COMMENT_NESTED: {
            s.line_has_comment = true;
            MultiLineRule* ml = &lang->multi_line[s.block_index];

            if (*p == ml->start[0] &&
             match_token(p, end, ml->start, ml->start_len)) {
                s.block_depth++;
                p += ml->start_len - 1;
            } else if (*p == ml->end[0] &&
             match_token(p, end, ml->end, ml->end_len)) {
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

        p++;
    }

    /*
     * Final line without trailing newline
     */
    if (s.line_has_code || s.line_has_comment ||
     (p > buf && !(char_table[(unsigned char)*(p - 1)] & CHAR_NEWLINE))) {
        finalize_line(&s, &c);
    }

    free(buf);

    return c;
}
