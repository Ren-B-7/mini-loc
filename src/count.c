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

        int hc = (int)has_code;
        int hcm = (int)has_comment & ~hc;
        int hb = 1 - hc - hcm;
        c->code += (uint32_t)hc;
        c->comment += (uint32_t)hcm;
        c->blank += (uint32_t)hb;

        /* Lines after the first in a span have neither code nor comment
         * by themselves — the state (string/block-comment) carries over,
         * so the *type* stays the same as the first line of the span.     */
        p = nl + 1;
    }
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

    FirstCharEntry dispatch[256];
    build_dispatch_table(lang, dispatch);

    Scanner s = {0};

    const char* p = buf;
    const char* end = buf + nread;

    while (p < end) {
        unsigned char ch = (unsigned char)*p;

        /* ── Newline ────────────────────────────────────────────────────────
         */
        if (__builtin_expect(char_table[ch] & CHAR_NEWLINE, 0)) {
            finalize_line(&s, &c);
            /*
             * Branchless line-comment reset:
             *   SCAN_LINE_COMMENT == 1, SCAN_NORMAL == 0.
             *   Multiplying by (state != LINE_COMMENT) zeroes the state when
             *   we are in a line comment, leaves it unchanged otherwise.
             *   Compiles to a cmov or a zeroing mov; no branch.
             */
            s.state = (ScanState)(s.state * (s.state != SCAN_LINE_COMMENT));
            p++;
            continue;
        }

        switch (s.state) {
        /* ── Normal code ────────────────────────────────────────────────────
         */
        case SCAN_NORMAL: {
            if (is_whitespace(ch)) {
                break;
            }

            FirstCharEntry* e = &dispatch[ch];

            /*
             * Merged fast-reject: read all three indices as a uint32_t and
             * compare against the all-0xFF sentinel in one shot.
             * For the vast majority of characters (alphanumerics etc.) this
             * collapses three sequential unpredictable branches into one
             * highly-predictable "nothing to do here" branch.
             */
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
                    /*
                     * Fast-forward to end of line with memchr.
                     * Eliminates per-character iterations through the switch
                     * for every line comment body — the most common hot path
                     * in heavily-commented code like the Linux kernel.
                     */
                    s.line_has_comment = true;
                    const char* nl =
                     (const char*)memchr(p, '\n', (size_t)(end - p));
                    /*
                     * Jump to the newline so the top-of-loop newline handler
                     * fires on the next iteration (handles finalize_line and
                     * state reset in one place).  If there is no newline the
                     * pointer advances to end and the loop exits.
                     */
                    p = nl ? nl : end;
                    continue; /* skip the p++ at the bottom */
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

        /*
         * SCAN_LINE_COMMENT is now unreachable inside the switch:
         * the fast-forward above jumps directly to the newline, so by the
         * time we re-enter the loop we are either on '\n' (handled at the
         * top) or past end.  The case is kept as a safe fallback.
         */
        case SCAN_LINE_COMMENT:
            s.line_has_comment = true;
            break;

        /* ── Block comment (flat) ───────────────────────────────────────────
         */
        case SCAN_BLOCK_COMMENT: {
            MultiLineRule* ml = &lang->multi_line[s.block_index];

            /*
             * Fast-forward: scan for the first byte of the end token with
             * memchr, then verify the full token only at candidate positions.
             * Skips every interior character without going through the switch.
             */
            const char* scan = p;
            while (scan < end) {
                const char* hit =
                 (const char*)memchr(scan, ml->end[0], (size_t)(end - scan));
                if (!hit) {
                    /* No end token in the rest of the buffer. */
                    count_newlines_in_span(p, end, false, true, &c);
                    s.line_has_comment = true;
                    p = end;
                    goto next_char; /* skip p++ */
                }
                if (match_token(hit, end, ml->end, ml->end_len)) {
                    /* Found closing token. */
                    count_newlines_in_span(p, hit, false, true, &c);
                    s.line_has_comment = true;
                    p = hit + ml->end_len;
                    s.state = SCAN_NORMAL;
                    goto next_char;
                }
                scan = hit + 1;
            }
            /* Exhausted buffer inside the while above via goto, but if we
             * somehow fall through, mark the current char as comment. */
            s.line_has_comment = true;
            break;
        }

        /* ── Block comment (nested) ─────────────────────────────────────────
         */
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

        /* ── String literal ─────────────────────────────────────────────────
         */
        case SCAN_STRING: {
            QuoteRule* q = &lang->quotes[s.quote_index];
            s.line_has_code = true;

            if (q->escape && (char_table[ch] & CHAR_BACKSLASH) && p + 1 < end) {
                p++;
                break;
            }

            if (!q->escape) {
                /*
                 * No escape sequences: fast-forward with memchr to the next
                 * candidate closing character, avoiding per-char loop overhead.
                 * Single-char end tokens are the common case ("…", '…').
                 */
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

        } /* switch */

        p++;
    next_char:;
    }

    /*
     * Final line without trailing newline.
     */
    if (s.line_has_code || s.line_has_comment ||
     (p > buf && !(char_table[(unsigned char)*(p - 1)] & CHAR_NEWLINE))) {
        finalize_line(&s, &c);
    }

    free(buf);

    return c;
}
