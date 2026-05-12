/*
 * loc_output.h — pluggable output formatters for mini-loc
 *
 * Supports four output formats:
 *   LOC_FMT_TERMINAL  — coloured ANSI table (original behaviour, default)
 *   LOC_FMT_JSON      — machine-readable JSON
 *   LOC_FMT_HTML      — self-contained HTML page with an inline style sheet
 *   LOC_FMT_SQL       — INSERT statements compatible with SQLite / PostgreSQL
 *
 * ── SQL SCHEMA ───────────────────────────────────────────────────────────────
 *
 *  CREATE TABLE IF NOT EXISTS loc_languages (
 *      run_id   TEXT    NOT NULL,
 *      language TEXT    NOT NULL,
 *      files    INTEGER NOT NULL,
 *      code     INTEGER NOT NULL,
 *      comment  INTEGER NOT NULL,
 *      blank    INTEGER NOT NULL,
 *      total    INTEGER NOT NULL,
 *      pct      REAL    NOT NULL,
 *      PRIMARY KEY (run_id, language)
 *  );
 *
 *  CREATE TABLE IF NOT EXISTS loc_files (
 *      run_id   TEXT    NOT NULL,
 *      path     TEXT    NOT NULL,
 *      ext      TEXT,
 *      language TEXT,
 *      code     INTEGER NOT NULL,
 *      comment  INTEGER NOT NULL,
 *      blank    INTEGER NOT NULL,
 *      total    INTEGER NOT NULL,
 *      PRIMARY KEY (run_id, path)
 *  );
 *
 *  The run_id is an ISO-8601 UTC timestamp generated once per invocation.
 *
 ******************************************************************************/

#ifndef LOC_OUTPUT_H
#define LOC_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Forward declarations ────────────────────────────────────────────────────
 *
 * These types must already be defined in the including translation unit
 * (they live in mini-loc-single.c / mini-loc-multi.c).  We only use them
 * through the pointers / arrays passed into each function, so a forward
 * declaration is sufficient.
 */
#ifndef LOC_TYPES_DEFINED
#define LOC_TYPES_DEFINED

#ifndef COUNTS_DEFINED
#define COUNTS_DEFINED

typedef struct {
	long code;
	long comment;
	long blank;
} Counts;
#endif

#ifndef LANGUAGE_DEFINED
#define LANGUAGE_DEFINED

typedef struct {
	size_t line_comment_lens[8];
	size_t block_start_lens[8];
	size_t block_end_lens[8];
	int n_extensions;
	int n_line_comments;
	int n_block_comments;
	bool data_only;
	char name[32];
	char line_comments[8][16];
	char block_start[8][16];
	char block_end[8][16];
	char extensions[32][32];
} Language;
#endif

#ifndef FILERESULT_DEFINED
#define FILERESULT_DEFINED

typedef struct {
	char* path;
	char* ext;
	int lang_idx;
	Counts counts;
} FileResult;
#endif

#endif

/* ── Public API ──────────────────────────────────────────────────────────────
 *
 * The Language and FileResult structures are defined above.
 */

typedef struct {
	int lang_idx;
	int files;
	Counts counts;
} LocLangSum;

typedef enum {
	LOC_FMT_TERMINAL = 0, /* coloured ANSI table — the default */
	LOC_FMT_JSON,
	LOC_FMT_HTML,
	LOC_FMT_SQL,
} LocOutputFormat;

/*
 * loc_print_report
 *
 * Master dispatcher — call this instead of (or from within) print_report().
 *
 * Parameters
 *   fmt        — output format selected by the user
 *   files      — g_files array (may be NULL if n_files == 0)
 *   n_files    — number of valid entries in files[]
 *   langs      — g_langs array
 *   n_langs    — number of valid entries in langs[]
 *   show_files — whether per-file rows should be included
 *   verbose    — whether the extension column should be shown (terminal / HTML)
 */
static inline void loc_print_report(LocOutputFormat fmt,
 const void* files,              /* FileResult*  — cast inside each formatter */
 int n_files, const void* langs, /* Language*    — cast inside each formatter */
 int n_langs, bool show_files, bool verbose);

/* Individual formatters — all static inline so they are inlined into the
 * single compilation unit that #includes this header, producing zero link-time
 * symbol conflicts between single and multi. */
static inline void loc_print_json(const void* files, int n_files,
 const void* langs, int n_langs, bool show_files);
static inline void loc_print_html(const void* files, int n_files,
 const void* langs, int n_langs, bool show_files, bool verbose);
static inline void loc_print_sql(const void* files, int n_files,
 const void* langs, int n_langs, bool show_files);

/* ── Internal helpers
 * ────────────────────────────────────────────────────────*/

/* Build the per-language summary table from a flat FileResult array.
 * Returns the number of entries written into out_sums (≤ max_sums).
 * Also writes grand totals into *t_files, *t_code, *t_comm, *t_blank. */
static inline int loc__build_sums(const void* files_v, int n_files, int n_langs,
 LocLangSum* out_sums, int max_sums, long* t_files, long* t_code, long* t_comm,
 long* t_blank);

/* qsort comparator — sort by total descending */
static int loc__sum_cmp(const void* a, const void* b)
{
	const LocLangSum* la = (const LocLangSum*) a;
	const LocLangSum* lb = (const LocLangSum*) b;
	long ta = la->counts.code + la->counts.comment + la->counts.blank;
	long tb = lb->counts.code + lb->counts.comment + lb->counts.blank;
	return (tb > ta) ? 1 : (tb < ta) ? -1 : 0;
}

/* Escape a string for JSON: replace " → \" and \ → \\ in-place into buf. */
static inline void loc__json_escape(const char* src, char* buf, size_t len)
{
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 2 < len; i++) {
		if (src[i] == '"' || src[i] == '\\') {
			buf[j++] = '\\';
		}
		buf[j++] = src[i];
	}
	buf[j] = '\0';
}

/* Escape a string for HTML: replace &, <, >, " with entities. */
static inline void loc__html_escape(const char* src, char* buf, size_t len)
{
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 8 < len; i++) {
		switch (src[i]) {
		case '&':
			memcpy(buf + j, "&amp;", 5);
			j += 5;
			break;
		case '<':
			memcpy(buf + j, "&lt;", 4);
			j += 4;
			break;
		case '>':
			memcpy(buf + j, "&gt;", 4);
			j += 4;
			break;
		case '"':
			memcpy(buf + j, "&quot;", 6);
			j += 6;
			break;
		case '\'':
			memcpy(buf + j, "&#39;", 5);
			j += 5;
			break;
		default:
			buf[j++] = src[i];
			break;
		}
	}
	buf[j] = '\0';
}

/* SQL single-quote escaping: replace ' → '' (ANSI SQL standard). */
static inline void loc__sql_escape(const char* src, char* buf, size_t len)
{
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 2 < len; i++) {
		if (src[i] == '\'') {
			buf[j++] = '\'';
		}
		buf[j++] = src[i];
	}
	buf[j] = '\0';
}

/* Generate an ISO-8601 UTC timestamp: "2025-05-12T14:30:00Z" */
static inline void loc__iso8601_now(char* buf, size_t len)
{
	time_t t = time(NULL);
	struct tm* gm = gmtime(&t);
	if (gm) {
		strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", gm);
	} else {
		strncpy(buf, "1970-01-01T00:00:00Z", len);
		buf[len - 1] = '\0';
	}
}

/* ── FileResult field accessors ──────────────────────────────────────────────
 *
 * Because FileResult is defined in the .c file we access its fields through
 * the typed pointer once it is visible.  When included inside the .c files
 * the full struct definition precedes this header, so the casts are valid.
 *
 * If you ever move FileResult to a shared header you can remove these macros.
 */
#define LOC__FR(arr, i) ((const FileResult*) (arr) + (i))
#define LOC__FR_PATH(arr, i) (LOC__FR(arr, i)->path)
#define LOC__FR_EXT(arr, i) (LOC__FR(arr, i)->ext)
#define LOC__FR_LANGIDX(arr, i) (LOC__FR(arr, i)->lang_idx)
#define LOC__FR_CODE(arr, i) (LOC__FR(arr, i)->counts.code)
#define LOC__FR_COMMENT(arr, i) (LOC__FR(arr, i)->counts.comment)
#define LOC__FR_BLANK(arr, i) (LOC__FR(arr, i)->counts.blank)

#define LOC__LANG(arr, i) ((const Language*) (arr) + (i))
#define LOC__LANG_NAME(arr, i) (LOC__LANG(arr, i)->name)

/* ── Build summary table
 * ─────────────────────────────────────────────────────*/

static inline int loc__build_sums(const void* files_v, int n_files, int n_langs,
 LocLangSum* out_sums, int max_sums, long* t_files, long* t_code, long* t_comm,
 long* t_blank)
{
	/* lang_to_sum_idx maps (lang_idx+1) → position in out_sums.
	 * We allocate on the heap to avoid VLA / large stack frames. */
	int map_size = n_langs + 2; /* +1 for the sentinel, +1 for unknown(-1) */
	int* lang_to_sum = (int*) malloc((size_t) map_size * sizeof(int));
	if (!lang_to_sum) {
		return 0;
	}
	memset(lang_to_sum, -1, (size_t) map_size * sizeof(int));

	int n_sums = 0;
	*t_files = *t_code = *t_comm = *t_blank = 0;

	for (int i = 0; i < n_files; i++) {
		int li = LOC__FR_LANGIDX(files_v, i);
		int map_idx = li + 1; /* -1 → 0, 0 → 1, … */
		if (map_idx < 0 || map_idx >= map_size) {
			continue;
		}

		int found = lang_to_sum[map_idx];
		if (found == -1) {
			if (n_sums >= max_sums) {
				continue;
			}
			found = n_sums++;
			out_sums[found].lang_idx = li;
			out_sums[found].files = 0;
			out_sums[found].counts.code = 0;
			out_sums[found].counts.comment = 0;
			out_sums[found].counts.blank = 0;
			lang_to_sum[map_idx] = found;
		}
		out_sums[found].files++;
		out_sums[found].counts.code += LOC__FR_CODE(files_v, i);
		out_sums[found].counts.comment += LOC__FR_COMMENT(files_v, i);
		out_sums[found].counts.blank += LOC__FR_BLANK(files_v, i);
	}
	free(lang_to_sum);

	qsort(out_sums, (size_t) n_sums, sizeof(LocLangSum), loc__sum_cmp);

	for (int i = 0; i < n_sums; i++) {
		*t_files += out_sums[i].files;
		*t_code += out_sums[i].counts.code;
		*t_comm += out_sums[i].counts.comment;
		*t_blank += out_sums[i].counts.blank;
	}
	return n_sums;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * JSON formatter
 * ═══════════════════════════════════════════════════════════════════════════
 */

static inline void loc_print_json(const void* files_v, int n_files,
 const void* langs_v, int n_langs, bool show_files)
{
#define MAX_SUMS_JSON 1024
	LocLangSum sums[MAX_SUMS_JSON];
	long t_files = 0, t_code = 0, t_comm = 0, t_blank = 0;

	int n_sums = loc__build_sums(files_v, n_files, n_langs, sums, MAX_SUMS_JSON,
	 &t_files, &t_code, &t_comm, &t_blank);
	long grand_total = t_code + t_comm + t_blank;

	char esc[1024];

	printf("{\n");

	/* ── languages array ── */
	printf("  \"languages\": [\n");
	for (int i = 0; i < n_sums; i++) {
		const char* name = (sums[i].lang_idx == -1) ?
		 "(unknown)" :
		 LOC__LANG_NAME(langs_v, sums[i].lang_idx);
		loc__json_escape(name, esc, sizeof(esc));
		long total =
		 sums[i].counts.code + sums[i].counts.comment + sums[i].counts.blank;
		double pct = (grand_total > 0) ?
		 100.0 * (double) total / (double) grand_total :
		 0.0;
		printf(
		 "    {\n"
		 "      \"language\": \"%s\",\n"
		 "      \"files\": %d,\n"
		 "      \"code\": %ld,\n"
		 "      \"comment\": %ld,\n"
		 "      \"blank\": %ld,\n"
		 "      \"total\": %ld,\n"
		 "      \"pct\": %.2f\n"
		 "    }%s\n",
		 esc, sums[i].files, sums[i].counts.code, sums[i].counts.comment,
		 sums[i].counts.blank, total, pct, (i < n_sums - 1) ? "," : "");
	}
	printf("  ],\n");

	/* ── totals ── */
	printf(
	 "  \"totals\": {\n"
	 "    \"files\": %ld,\n"
	 "    \"code\": %ld,\n"
	 "    \"comment\": %ld,\n"
	 "    \"blank\": %ld,\n"
	 "    \"total\": %ld\n"
	 "  }",
	 t_files, t_code, t_comm, t_blank, grand_total);

	/* ── per-file results (optional) ── */
	if (show_files && n_files > 0) {
		printf(",\n  \"files\": [\n");
		for (int i = 0; i < n_files; i++) {
			const char* path = LOC__FR_PATH(files_v, i);
			const char* ext = LOC__FR_EXT(files_v, i);
			int li = LOC__FR_LANGIDX(files_v, i);
			const char* lang =
			 (li >= 0 && li < n_langs) ? LOC__LANG_NAME(langs_v, li) : "(unknown)";

			char esc_path[4096], esc_lang[128];
			loc__json_escape(path ? path : "", esc_path, sizeof(esc_path));
			loc__json_escape(lang, esc_lang, sizeof(esc_lang));

			long code = LOC__FR_CODE(files_v, i);
			long comment = LOC__FR_COMMENT(files_v, i);
			long blank = LOC__FR_BLANK(files_v, i);
			long total = code + comment + blank;

			printf(
			 "    {\n"
			 "      \"path\": \"%s\",\n"
			 "      \"ext\": \"%s\",\n"
			 "      \"language\": \"%s\",\n"
			 "      \"code\": %ld,\n"
			 "      \"comment\": %ld,\n"
			 "      \"blank\": %ld,\n"
			 "      \"total\": %ld\n"
			 "    }%s\n",
			 esc_path, ext ? ext : "", esc_lang, code, comment, blank, total,
			 (i < n_files - 1) ? "," : "");
		}
		printf("  ]");
	}

	printf("\n}\n");
#undef MAX_SUMS_JSON
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HTML formatter
 * ═══════════════════════════════════════════════════════════════════════════
 */

static inline void loc_print_html(const void* files_v, int n_files,
 const void* langs_v, int n_langs, bool show_files, bool verbose)
{
#define MAX_SUMS_HTML 1024
	LocLangSum sums[MAX_SUMS_HTML];
	long t_files = 0, t_code = 0, t_comm = 0, t_blank = 0;

	int n_sums = loc__build_sums(files_v, n_files, n_langs, sums, MAX_SUMS_HTML,
	 &t_files, &t_code, &t_comm, &t_blank);
	long grand_total = t_code + t_comm + t_blank;

	char esc[4096];

	/* ── Document head + inline CSS ── */
	printf(
	 "<!DOCTYPE html>\n"
	 "<html lang=\"en\">\n"
	 "<head>\n"
	 "  <meta charset=\"UTF-8\">\n"
	 "  <meta name=\"viewport\" "
	 "content=\"width=device-width,initial-scale=1\">\n"
	 "  <title>mini-loc report</title>\n"
	 "  <style>\n"
	 "    :root {\n"
	 "      --bg:       #1e1e2e;\n"
	 "      --surface:  #2a2a3c;\n"
	 "      --border:   #44475a;\n"
	 "      --fg:       #cdd6f4;\n"
	 "      --muted:    #6c7086;\n"
	 "      --green:    #a6e3a1;\n"
	 "      --yellow:   #f9e2af;\n"
	 "      --cyan:     #89dceb;\n"
	 "      --red:      #f38ba8;\n"
	 "      --bar-bg:   #313244;\n"
	 "    }\n"
	 "    * { box-sizing: border-box; margin: 0; padding: 0; }\n"
	 "    body {\n"
	 "      font-family: 'Consolas', 'Menlo', 'DejaVu Sans Mono', monospace;\n"
	 "      background: var(--bg); color: var(--fg);\n"
	 "      padding: 2rem; font-size: 14px;\n"
	 "    }\n"
	 "    h1 { color: var(--cyan); margin-bottom: 1.5rem; font-size: 1.4rem; "
	 "}\n"
	 "    h2 { color: var(--cyan); margin: 2rem 0 0.75rem; font-size: 1rem;\n"
	 "         text-transform: uppercase; letter-spacing: 0.08em; }\n"
	 "    table { width: 100%%; border-collapse: collapse; margin-bottom: "
	 "1rem; }\n"
	 "    th {\n"
	 "      text-align: right; padding: 0.4rem 0.6rem;\n"
	 "      border-bottom: 1px solid var(--border);\n"
	 "      color: var(--muted); font-weight: normal;\n"
	 "    }\n"
	 "    th:first-child { text-align: left; }\n"
	 "    td { padding: 0.35rem 0.6rem; text-align: right; }\n"
	 "    td:first-child { text-align: left; }\n"
	 "    tr:hover td { background: var(--surface); }\n"
	 "    .total-row td {\n"
	 "      border-top: 1px solid var(--border); font-weight: bold;\n"
	 "    }\n"
	 "    .code    { color: var(--green);  }\n"
	 "    .comment { color: var(--yellow); }\n"
	 "    .blank   { color: var(--muted);  }\n"
	 "    .pct-cell { min-width: 120px; }\n"
	 "    .bar-wrap {\n"
	 "      display: inline-block; width: 80px; height: 8px;\n"
	 "      background: var(--bar-bg); border-radius: 4px;\n"
	 "      vertical-align: middle; margin-right: 4px;\n"
	 "    }\n"
	 "    .bar-fill {\n"
	 "      display: block; height: 100%%; border-radius: 4px;\n"
	 "      background: var(--cyan);\n"
	 "    }\n"
	 "    .breakdown {\n"
	 "      display: flex; gap: 2rem; margin-top: 1rem;\n"
	 "      font-size: 0.9rem;\n"
	 "    }\n"
	 "    .breakdown span { display: flex; align-items: center; gap: 0.4rem; "
	 "}\n"
	 "    .dot {\n"
	 "      display: inline-block; width: 10px; height: 10px;\n"
	 "      border-radius: 50%%;\n"
	 "    }\n"
	 "  </style>\n"
	 "</head>\n"
	 "<body>\n"
	 "  <h1>&#128196; mini-loc &mdash; Language Summary</h1>\n");

	/* ── Language summary table ── */
	printf(
	 "  <h2>Languages</h2>\n"
	 "  <table>\n"
	 "    <thead>\n"
	 "      <tr>\n"
	 "        <th>Language</th><th>Files</th><th class=\"code\">Code</th>\n"
	 "        <th>%%</th><th class=\"comment\">Comment</th>\n"
	 "        <th class=\"blank\">Blank</th><th>Total</th>\n"
	 "      </tr>\n"
	 "    </thead>\n"
	 "    <tbody>\n");

	for (int i = 0; i < n_sums; i++) {
		const char* name = (sums[i].lang_idx == -1) ?
		 "(unknown)" :
		 LOC__LANG_NAME(langs_v, sums[i].lang_idx);
		loc__html_escape(name, esc, sizeof(esc));
		long total =
		 sums[i].counts.code + sums[i].counts.comment + sums[i].counts.blank;
		double pct = (grand_total > 0) ?
		 100.0 * (double) total / (double) grand_total :
		 0.0;

		printf(
		 "      <tr>\n"
		 "        <td>%s</td>\n"
		 "        <td>%d</td>\n"
		 "        <td class=\"code\">%ld</td>\n"
		 "        <td class=\"pct-cell\">"
		 "<span class=\"bar-wrap\">"
		 "<span class=\"bar-fill\" style=\"width:%.1f%%\">"
		 "</span>"
		 "</span>%.1f%%</td>\n"
		 "        <td class=\"comment\">%ld</td>\n"
		 "        <td class=\"blank\">%ld</td>\n"
		 "        <td>%ld</td>\n"
		 "      </tr>\n",
		 esc, sums[i].files, sums[i].counts.code, pct, pct,
		 sums[i].counts.comment, sums[i].counts.blank, total);
	}

	/* totals row */
	printf(
	 "    </tbody>\n"
	 "    <tfoot>\n"
	 "      <tr class=\"total-row\">\n"
	 "        <td>TOTAL</td>\n"
	 "        <td>%ld</td>\n"
	 "        <td class=\"code\">%ld</td>\n"
	 "        <td>100.0%%</td>\n"
	 "        <td class=\"comment\">%ld</td>\n"
	 "        <td class=\"blank\">%ld</td>\n"
	 "        <td>%ld</td>\n"
	 "      </tr>\n"
	 "    </tfoot>\n"
	 "  </table>\n",
	 t_files, t_code, t_comm, t_blank, grand_total);

	/* breakdown legend */
	if (grand_total > 0) {
		double c_pct = 100.0 * (double) t_code / (double) grand_total;
		double m_pct = 100.0 * (double) t_comm / (double) grand_total;
		double b_pct = 100.0 * (double) t_blank / (double) grand_total;
		printf(
		 "  <div class=\"breakdown\">\n"
		 "    <span><span class=\"dot\" "
		 "style=\"background:var(--green)\"></span>"
		 "Code %.1f%%</span>\n"
		 "    <span><span class=\"dot\" "
		 "style=\"background:var(--yellow)\"></span>"
		 "Comment %.1f%%</span>\n"
		 "    <span><span class=\"dot\" "
		 "style=\"background:var(--muted)\"></span>"
		 "Blank %.1f%%</span>\n"
		 "  </div>\n",
		 c_pct, m_pct, b_pct);
	}

	/* ── Per-file table (optional) ── */
	if (show_files && n_files > 0) {
		printf(
		 "  <h2>Files</h2>\n"
		 "  <table>\n"
		 "    <thead>\n"
		 "      <tr>\n"
		 "        <th>Path</th>\n");
		if (verbose) {
			printf("        <th>Ext</th>\n");
		}
		printf(
		 "        <th class=\"code\">Code</th>\n"
		 "        <th class=\"comment\">Comment</th>\n"
		 "        <th class=\"blank\">Blank</th>\n"
		 "        <th>Total</th>\n"
		 "      </tr>\n"
		 "    </thead>\n"
		 "    <tbody>\n");

		for (int i = 0; i < n_files; i++) {
			const char* path = LOC__FR_PATH(files_v, i);
			const char* ext = LOC__FR_EXT(files_v, i);
			long code = LOC__FR_CODE(files_v, i);
			long comment = LOC__FR_COMMENT(files_v, i);
			long blank = LOC__FR_BLANK(files_v, i);
			long total = code + comment + blank;

			char esc_path[4096];
			loc__html_escape(path ? path : "", esc_path, sizeof(esc_path));

			printf("      <tr>\n"
			       "        <td>%s</td>\n",
			 esc_path);
			if (verbose) {
				char esc_ext[64];
				loc__html_escape(ext ? ext : "", esc_ext, sizeof(esc_ext));
				printf("        <td>%s</td>\n", esc_ext);
			}
			printf(
			 "        <td class=\"code\">%ld</td>\n"
			 "        <td class=\"comment\">%ld</td>\n"
			 "        <td class=\"blank\">%ld</td>\n"
			 "        <td>%ld</td>\n"
			 "      </tr>\n",
			 code, comment, blank, total);
		}
		printf("    </tbody>\n"
		       "  </table>\n");
	}

	printf("</body>\n</html>\n");
#undef MAX_SUMS_HTML
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SQL formatter
 * ═══════════════════════════════════════════════════════════════════════════
 */

static inline void loc_print_sql(const void* files_v, int n_files,
 const void* langs_v, int n_langs, bool show_files)
{
#define MAX_SUMS_SQL 1024
	LocLangSum sums[MAX_SUMS_SQL];
	long t_files = 0, t_code = 0, t_comm = 0, t_blank = 0;

	int n_sums = loc__build_sums(files_v, n_files, n_langs, sums, MAX_SUMS_SQL,
	 &t_files, &t_code, &t_comm, &t_blank);
	long grand_total = t_code + t_comm + t_blank;

	char ts[32];
	loc__iso8601_now(ts, sizeof(ts));

	char esc[4096];

	/* ── DDL ── */
	printf("-- mini-loc SQL export — run at %s\n\n", ts);

	printf(
	 "CREATE TABLE IF NOT EXISTS loc_languages (\n"
	 "    run_id   TEXT    NOT NULL,\n"
	 "    language TEXT    NOT NULL,\n"
	 "    files    INTEGER NOT NULL,\n"
	 "    code     INTEGER NOT NULL,\n"
	 "    comment  INTEGER NOT NULL,\n"
	 "    blank    INTEGER NOT NULL,\n"
	 "    total    INTEGER NOT NULL,\n"
	 "    pct      REAL    NOT NULL,\n"
	 "    PRIMARY KEY (run_id, language)\n"
	 ");\n\n");

	if (show_files) {
		printf(
		 "CREATE TABLE IF NOT EXISTS loc_files (\n"
		 "    run_id   TEXT    NOT NULL,\n"
		 "    path     TEXT    NOT NULL,\n"
		 "    ext      TEXT,\n"
		 "    language TEXT,\n"
		 "    code     INTEGER NOT NULL,\n"
		 "    comment  INTEGER NOT NULL,\n"
		 "    blank    INTEGER NOT NULL,\n"
		 "    total    INTEGER NOT NULL,\n"
		 "    PRIMARY KEY (run_id, path)\n"
		 ");\n\n");
	}

	/* ── Language rows ── */
	printf("-- Language summary\n");
	for (int i = 0; i < n_sums; i++) {
		const char* name = (sums[i].lang_idx == -1) ?
		 "(unknown)" :
		 LOC__LANG_NAME(langs_v, sums[i].lang_idx);
		loc__sql_escape(name, esc, sizeof(esc));
		long total =
		 sums[i].counts.code + sums[i].counts.comment + sums[i].counts.blank;
		double pct = (grand_total > 0) ?
		 100.0 * (double) total / (double) grand_total :
		 0.0;

		printf("INSERT INTO loc_languages"
		       " (run_id, language, files, code, comment, blank, total, pct)"
		       " VALUES ('%s', '%s', %d, %ld, %ld, %ld, %ld, %.4f);\n",
		 ts, esc, sums[i].files, sums[i].counts.code, sums[i].counts.comment,
		 sums[i].counts.blank, total, pct);
	}
	printf("\n");

	/* ── Per-file rows (optional) ── */
	if (show_files && n_files > 0) {
		printf("-- Per-file results\n");
		for (int i = 0; i < n_files; i++) {
			const char* path = LOC__FR_PATH(files_v, i);
			const char* ext = LOC__FR_EXT(files_v, i);
			int li = LOC__FR_LANGIDX(files_v, i);
			const char* lang =
			 (li >= 0 && li < n_langs) ? LOC__LANG_NAME(langs_v, li) : "(unknown)";

			char esc_path[4096], esc_ext[64], esc_lang[128];
			loc__sql_escape(path ? path : "", esc_path, sizeof(esc_path));
			loc__sql_escape(ext ? ext : "", esc_ext, sizeof(esc_ext));
			loc__sql_escape(lang, esc_lang, sizeof(esc_lang));

			long code = LOC__FR_CODE(files_v, i);
			long comment = LOC__FR_COMMENT(files_v, i);
			long blank = LOC__FR_BLANK(files_v, i);
			long total = code + comment + blank;

			printf("INSERT INTO loc_files"
			       " (run_id, path, ext, language, code, comment, blank, total)"
			       " VALUES ('%s', '%s', '%s', '%s', %ld, %ld, %ld, %ld);\n",
			 ts, esc_path, esc_ext, esc_lang, code, comment, blank, total);
		}
		printf("\n");
	}
#undef MAX_SUMS_SQL
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Master dispatcher
 * ═══════════════════════════════════════════════════════════════════════════
 */

static inline void loc_print_report(LocOutputFormat fmt, const void* files,
 int n_files, const void* langs, int n_langs, bool show_files, bool verbose)
{
	switch (fmt) {
	case LOC_FMT_JSON:
		loc_print_json(files, n_files, langs, n_langs, show_files);
		break;
	case LOC_FMT_HTML:
		loc_print_html(files, n_files, langs, n_langs, show_files, verbose);
		break;
	case LOC_FMT_SQL:
		loc_print_sql(files, n_files, langs, n_langs, show_files);
		break;
	case LOC_FMT_TERMINAL:
	default:
		/* Caller keeps its own print_report() for the ANSI terminal view.
		 * To route terminal output through this header too, add a
		 * loc_print_terminal() function and call it here. */
		break;
	}
}

/* ── Cleanup macros ── */
#undef LOC__FR
#undef LOC__FR_PATH
#undef LOC__FR_EXT
#undef LOC__FR_LANGIDX
#undef LOC__FR_CODE
#undef LOC__FR_COMMENT
#undef LOC__FR_BLANK
#undef LOC__LANG
#undef LOC__LANG_NAME

#endif /* LOC_OUTPUT_H */
