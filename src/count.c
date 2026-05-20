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

/*
 * Dispatch entry: three uint8_t indices, all 0xFF = "no rule".
 * Laid out as a uint32_t so we can test all three with one comparison:
 *   (*(uint32_t*)e & 0x00FFFFFF) == 0x00FFFFFF  =>  nothing active.
 * The fourth byte is padding kept at 0xFF to make the mask safe.
 */
typedef struct {
    uint8_t quote_idx;
    uint8_t line_comment_idx;
    uint8_t block_comment_idx;
    uint8_t _pad; /* always 0xFF */
} FirstCharEntry;

static void build_dispatch_table(Language* lang, FirstCharEntry* dispatch)
{
    memset(dispatch, 0xFF, sizeof(FirstCharEntry) * 256);
    for (int i = 0; i < lang->n_quotes; i++) {
        dispatch[(uint8_t)lang->quotes[i].start[0]].quote_idx = (uint8_t)i;
    }
    for (int i = 0; i < lang->n_line_comments; i++) {
        dispatch[(uint8_t)lang->line_comments[i].start[0]].line_comment_idx =
         (uint8_t)i;
    }
    for (int i = 0; i < lang->n_multi_line; i++) {
        dispatch[(uint8_t)lang->multi_line[i].start[0]].block_comment_idx =
         (uint8_t)i;
    }
}

static inline __attribute__((always_inline)) void
finalize_line(Scanner* s, Counts* c)
{
    c->code += (uint32_t)s->line_has_code;
    c->comment += (uint32_t)(s->line_has_comment && !s->line_has_code);
    c->blank += (uint32_t)(!(s->line_has_code || s->line_has_comment));

    s->line_has_code = false;
    s->line_has_comment = false;
}

/*
 * Count newlines between [p, fence) and finalize each line.
 * Used by the fast-forward helpers to account for multi-line spans.
 */
static inline __attribute__((always_inline)) void
count_newlines_in_span(const char* p, const char* fence, bool has_code,
 bool has_comment, Counts* c)
{
    while (p < fence) {
        const char* nl = (const char*)memchr(p, '\n', (size_t)(fence - p));
        if (!nl) {
            break;
        }

        /* finalize the line ending at nl */
        bool hco = has_code;
        bool hcm = (has_comment && !has_code);
        bool hb = !(has_code || has_comment);

        c->code += (uint32_t)hco;
        c->comment += (uint32_t)hcm;
        c->blank += (uint32_t)hb;

        p = nl + 1;
    }
}

/* ──────────────────────────────────────────────────────────────────────────
 * Standard Counting Path (Zero Overhead)
 * ────────────────────────────────────────────────────────────────────────── */

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
    if (file_len <= 0 || file_len >= MAX_FILE_SIZE) {
        fclose(f);
        return c;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return c;
    }
    size_t size = (size_t)file_len;
    char* buf = calloc(size + 1, 1);
    if (!buf) {
        fclose(f);
        return c;
    }
    size_t nread = fread(buf, 1, size, f);
    fclose(f);
    if (nread > size) {
        nread = size;
    }

    Language* lang =
     (lang_idx >= 0 && lang_idx < MAX_LANGS) ? &g_langs[lang_idx] : NULL;
    if (!lang) {
        free(buf);
        return c;
    }

    FirstCharEntry dispatch[256];
    build_dispatch_table(lang, dispatch);

    Scanner s = {0};
    const char* p = buf;
    const char* end = buf + nread;

    while (p < end) {
        unsigned char ch = (unsigned char)*p;

        if (__builtin_expect(char_table[ch] & CHAR_NEWLINE, 0)) {
            finalize_line(&s, &c);
            s.state = (ScanState)(s.state * (s.state != SCAN_LINE_COMMENT));
            p++;
            continue;
        }

        switch (s.state) {
        case SCAN_NORMAL: {
            if (is_whitespace(ch)) {
                break;
            }

            FirstCharEntry* e = &dispatch[ch];
            uint32_t ent;
            memcpy(&ent, e, 4);

            if (__builtin_expect((ent & 0x00FFFFFFu) == 0x00FFFFFFu, 1)) {
                s.line_has_code = true;
                break;
            }

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
                    s.line_has_comment = true;
                    const char* nl =
                     (const char*)memchr(p, '\n', (size_t)(end - p));
                    p = nl ? nl : end;
                    continue;
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
            MultiLineRule* ml = &lang->multi_line[s.block_index];
            const char* scan = p;
            while (scan < end) {
                const char* hit =
                 (const char*)memchr(scan, ml->end[0], (size_t)(end - scan));
                if (!hit) {
                    count_newlines_in_span(p, end, false, true, &c);
                    s.line_has_comment = true;
                    p = end;
                    goto next_char;
                }
                if (match_token(hit, end, ml->end, ml->end_len)) {
                    count_newlines_in_span(p, hit, false, true, &c);
                    s.line_has_comment = true;
                    p = hit + ml->end_len;
                    s.state = SCAN_NORMAL;
                    goto next_char;
                }
                scan = hit + 1;
            }
            s.line_has_comment = true;
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
            if (q->escape && (char_table[ch] & CHAR_BACKSLASH) && p + 1 < end) {
                p++;
                break;
            }
            if (!q->escape) {
                if (q->end_len == 1) {
                    const char* hit =
                     (const char*)memchr(p, q->end[0], (size_t)(end - p));
                    if (!hit) {
                        count_newlines_in_span(p, end, true, false, &c);
                        p = end;
                        goto next_char;
                    }
                    count_newlines_in_span(p, hit, true, false, &c);
                    s.line_has_code = true;
                    p = hit + 1;
                    s.state = SCAN_NORMAL;
                    goto next_char;
                }
            }
            if (*p == q->end[0] && match_token(p, end, q->end, q->end_len)) {
                p += q->end_len - 1;
                s.state = SCAN_NORMAL;
            }
            break;
        }
        }
        p++;
    next_char:;
    }

    if (s.line_has_code || s.line_has_comment ||
     (p > buf && !(char_table[(unsigned char)*(p - 1)] & CHAR_NEWLINE))) {
        finalize_line(&s, &c);
    }

    free(buf);
    return c;
}

/* ──────────────────────────────────────────────────────────────────────────
 * Complexity Counting Path Duplicate of count_file BUT has a different branch
 * setup for complexity counting. COMPLEXITY SKIPS COMMENTS ENTIRELY
 * ────────────────────────────────────────────────────────────────────────── */

Counts count_file_complexity(const char* path, int lang_idx)
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
    if (file_len <= 0 || file_len >= MAX_FILE_SIZE) {
        fclose(f);
        return c;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return c;
    }
    size_t size = (size_t)file_len;
    char* buf = calloc(size + 1, 1);
    if (!buf) {
        fclose(f);
        return c;
    }
    size_t nread = fread(buf, 1, size, f);
    fclose(f);
    if (nread > size) {
        nread = size;
    }

    Language* lang =
     (lang_idx >= 0 && lang_idx < MAX_LANGS) ? &g_langs[lang_idx] : NULL;
    if (!lang) {
        free(buf);
        return c;
    }

    FirstCharEntry dispatch[256];
    build_dispatch_table(lang, dispatch);

    Scanner s = {0};
    const char* p = buf;
    const char* end = buf + nread;

    while (p < end) {
        unsigned char ch = (unsigned char)*p;

        if (__builtin_expect(char_table[ch] & CHAR_NEWLINE, 0)) {
            finalize_line(&s, &c);
            s.state = (ScanState)(s.state * (s.state != SCAN_LINE_COMMENT));
            p++;
            continue;
        }

        switch (s.state) {
        case SCAN_NORMAL: {
            if (is_whitespace(ch)) {
                break;
            }

            FirstCharEntry* e = &dispatch[ch];
            uint32_t ent;
            memcpy(&ent, e, 4);

            if (__builtin_expect((ent & 0x00FFFFFFu) == 0x00FFFFFFu, 1)) {
                s.line_has_code = true;
                goto check_complexity;
            }

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
                    s.line_has_comment = true;
                    const char* nl =
                     (const char*)memchr(p, '\n', (size_t)(end - p));
                    p = nl ? nl : end;
                    continue;
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

        check_complexity:
            if ((g_complexity_starts[lang_idx][ch >> 5] >> (ch & 0x1F)) & 1) {
                for (int ci = 0; ci < lang->n_complexity; ci++) {
                    ComplexityRule* cr = &lang->complexity[ci];
                    if ((unsigned char)cr->token[0] != ch) {
                        continue;
                    }
                    if (!match_token(p, end, cr->token, cr->len)) {
                        continue;
                    }
                    bool prev_ok = (p == buf) ||
                     !((unsigned char)*(p - 1) == '_' ||
                      ((unsigned char)*(p - 1) >= 'a' &&
                       (unsigned char)*(p - 1) <= 'z') ||
                      ((unsigned char)*(p - 1) >= 'A' &&
                       (unsigned char)*(p - 1) <= 'Z') ||
                      ((unsigned char)*(p - 1) >= '0' &&
                       (unsigned char)*(p - 1) <= '9'));
                    const char* after = p + cr->len;
                    bool next_ok = (after >= end) ||
                     !((unsigned char)*after == '_' ||
                      ((unsigned char)*after >= 'a' &&
                       (unsigned char)*after <= 'z') ||
                      ((unsigned char)*after >= 'A' &&
                       (unsigned char)*after <= 'Z') ||
                      ((unsigned char)*after >= '0' &&
                       (unsigned char)*after <= '9'));
                    if (prev_ok && next_ok) {
                        c.complexity++;
                        p += cr->len - 1;
                    }
                    break;
                }
            }
            break;
        }

        case SCAN_LINE_COMMENT:
            s.line_has_comment = true;
            break;

        case SCAN_BLOCK_COMMENT: {
            MultiLineRule* ml = &lang->multi_line[s.block_index];
            const char* scan = p;
            while (scan < end) {
                const char* hit =
                 (const char*)memchr(scan, ml->end[0], (size_t)(end - scan));
                if (!hit) {
                    count_newlines_in_span(p, end, false, true, &c);
                    s.line_has_comment = true;
                    p = end;
                    goto next_char;
                }
                if (match_token(hit, end, ml->end, ml->end_len)) {
                    count_newlines_in_span(p, hit, false, true, &c);
                    s.line_has_comment = true;
                    p = hit + ml->end_len;
                    s.state = SCAN_NORMAL;
                    goto next_char;
                }
                scan = hit + 1;
            }
            s.line_has_comment = true;
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
            if (q->escape && (char_table[ch] & CHAR_BACKSLASH) && p + 1 < end) {
                p++;
                break;
            }
            if (!q->escape) {
                if (q->end_len == 1) {
                    const char* hit =
                     (const char*)memchr(p, q->end[0], (size_t)(end - p));
                    if (!hit) {
                        count_newlines_in_span(p, end, true, false, &c);
                        p = end;
                        goto next_char;
                    }
                    count_newlines_in_span(p, hit, true, false, &c);
                    s.line_has_code = true;
                    p = hit + 1;
                    s.state = SCAN_NORMAL;
                    goto next_char;
                }
            }
            if (*p == q->end[0] && match_token(p, end, q->end, q->end_len)) {
                p += q->end_len - 1;
                s.state = SCAN_NORMAL;
            }
            break;
        }
        }
        p++;
    next_char:;
    }

    if (s.line_has_code || s.line_has_comment ||
     (p > buf && !(char_table[(unsigned char)*(p - 1)] & CHAR_NEWLINE))) {
        finalize_line(&s, &c);
    }

    free(buf);
    return c;
}
