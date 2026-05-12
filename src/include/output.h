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

#include "types.h"

/* Public types */

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
 const void* files,              /* FileResult* — cast inside each formatter */
 int n_files, const void* langs, /* Language* — cast inside each formatter */
 int n_langs, bool show_files, bool verbose);

/* Individual formatters — all static inline so they are inlined into the
 * single compilation unit that #includes this header, producing zero link-time
 * symbol conflicts between single and multi. */
static void loc_print_json(const void* files, int n_files, const void* langs,
 int n_langs, bool show_files);
static inline void loc_print_html(const void* files, int n_files,
 const void* langs, int n_langs, bool show_files, bool verbose);
static void loc_print_sql(const void* files, int n_files, const void* langs,
 int n_langs, bool show_files);
static inline void loc_print_terminal(const void* files, int n_files,
 const void* langs, int n_langs, bool show_files, bool verbose);

/* Internal helpers */

/* Build the per-language summary table from a flat FileResult array.
 * Returns the number of entries written into out_sums (≤ max_sums).
 * Also writes grand totals into *t_files, *t_code, *t_comm, *t_blank. */
static int loc__build_sums(const void* files_v, int n_files, int n_langs,
 LocLangSum* out_sums, int max_sums, long* t_files, long* t_code, long* t_comm,
 long* t_blank);

/* qsort comparator — sort by total descending */
static inline int loc__sum_cmp(const void* a, const void* b)
{
	const LocLangSum* la = (const LocLangSum*) a;
	const LocLangSum* lb = (const LocLangSum*) b;
	long ta = la->counts.code + la->counts.comment + la->counts.blank;
	long tb = lb->counts.code + lb->counts.comment + lb->counts.blank;
	return (tb > ta) ? 1 : (tb < ta) ? -1 : 0;
}

/* Escape a string for JSON: replace " -> \" and \ -> \\ in-place into buf. */
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

/* SQL single-quote escaping: replace ' -> '' (ANSI SQL standard). */
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

/* HTML helpers */
#define LOC_HTML_HEADER \
	"<!DOCTYPE html>\n" \
	"<html lang=\"en\">\n" \
	"<head>\n" \
	"<meta charset=\"utf-8\">\n" \
	"<title>mini-loc</title>\n" \
	"<style>\n" \
	"body{font-family:monospace;margin:20px;}\n" \
	"table{border-collapse:collapse;}\n" \
	"th,td{border:1px solid #ccc;padding:4px 8px;text-align:right;}\n" \
	"th:first-child,td:first-child{text-align:left;}\n" \
	"</style>\n" \
	"</head>\n" \
	"<body>\n"

#define LOC_HTML_FOOTER \
	"</body>\n" \
	"</html>\n"

/* Terminal helpers */
#define LOC_TERM_RESET "\033[0m"
#define LOC_TERM_CYAN "\033[36m"
#define LOC_TERM_GREEN "\033[32m"
#define LOC_TERM_YELLOW "\033[33m"
#define LOC_TERM_GRAY "\033[90m"

#define LOC__FR(arr, i) ((const FileResult*) (arr) + (i))
#define LOC__FR_PATH(arr, i) (LOC__FR(arr, i)->path)
#define LOC__FR_EXT(arr, i) (LOC__FR(arr, i)->ext)
#define LOC__FR_LANGIDX(arr, i) (LOC__FR(arr, i)->lang_idx)
#define LOC__FR_CODE(arr, i) (LOC__FR(arr, i)->counts.code)
#define LOC__FR_COMMENT(arr, i) (LOC__FR(arr, i)->counts.comment)
#define LOC__FR_BLANK(arr, i) (LOC__FR(arr, i)->counts.blank)

#define LOC__LANG(arr, i) ((const Language*) (arr) + (i))
#define LOC__LANG_NAME(arr, i) (LOC__LANG(arr, i)->name)

/* Build summary table */

static int loc__build_sums(const void* files_v, int num_files, int num_langs,
 LocLangSum* out_sums, int max_sums, long* t_files, long* t_code, long* t_comm,
 long* t_blank)
{
	/* lang_to_sum_idx maps (lang_idx+1) → position in out_sums.
	 * Using a stack array since MAX_LANGS is small. */
	int map_size = num_langs + 2; /* +1 for the sentinel, +1 for unknown(-1) */
	int lang_to_sum[MAX_LANGS + 2];
	if (map_size > (int)(sizeof(lang_to_sum) / sizeof(int))) {
		return 0;
	}
	for (int i = 0; i < map_size; i++) {
		lang_to_sum[i] = -1;
	}

	int n_sums = 0;
	*t_files = *t_code = *t_comm = *t_blank = 0;

	for (int i = 0; i < num_files; i++) {
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

	qsort(out_sums, (size_t) n_sums, sizeof(LocLangSum), loc__sum_cmp);

	for (int i = 0; i < n_sums; i++) {
		*t_files += out_sums[i].files;
		*t_code += out_sums[i].counts.code;
		*t_comm += out_sums[i].counts.comment;
		*t_blank += out_sums[i].counts.blank;
	}
	return n_sums;
}

/*
 * JSON formatter
 */

static void loc_print_json(const void* files_v, int n_files,
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
			const char* lang = (li >= 0 && li < n_langs) ?
			 LOC__LANG_NAME(langs_v, li) :
			 "(unknown)";

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

/*
 * HTML formatter
 */

static inline void loc_print_html(const void* files_v, int n_files,
 const void* langs_v, int n_langs, bool show_files, bool verbose)
{
	(void) verbose;

#define MAX_SUMS_HTML 1024

	LocLangSum* sums = calloc(MAX_SUMS_HTML, sizeof(LocLangSum));

	if (!sums) {
		return;
	}

	long t_files = 0;
	long t_code = 0;
	long t_comment = 0;
	long t_blank = 0;

	int n_sums = loc__build_sums(files_v, n_files, n_langs, sums, MAX_SUMS_HTML,
	 &t_files, &t_code, &t_comment, &t_blank);

	long grand_total = t_code + t_comment + t_blank;

	char esc[4096];

	printf("%s", LOC_HTML_HEADER);

	printf(
	 "<table>\n"
	 "<thead>\n"
	 "<tr>\n"
	 "<th>Language</th>\n"
	 "<th>Files</th>\n"
	 "<th>Code</th>\n"
	 "<th>Comment</th>\n"
	 "<th>Blank</th>\n"
	 "<th>Total</th>\n"
	 "<th>%%</th>\n"
	 "</tr>\n"
	 "</thead>\n"
	 "<tbody>\n");

	for (int i = 0; i < n_sums; i++) {
		const char* name = (sums[i].lang_idx == -1) ?
		 "(unknown)" :
		 LOC__LANG_NAME(langs_v, sums[i].lang_idx);

		loc__html_escape(name, esc, sizeof(esc));

		long total =
		 sums[i].counts.code + sums[i].counts.comment + sums[i].counts.blank;

		double pct = (grand_total > 0) ?
		 (100.0 * (double) total / (double) grand_total) :
		 0.0;

		printf(
		 "<tr>"
		 "<td>%s</td>"
		 "<td>%d</td>"
		 "<td>%ld</td>"
		 "<td>%ld</td>"
		 "<td>%ld</td>"
		 "<td>%ld</td>"
		 "<td>%.1f</td>"
		 "</tr>\n",
		 esc, sums[i].files, sums[i].counts.code, sums[i].counts.comment,
		 sums[i].counts.blank, total, pct);
	}

	printf(
	 "<tr>"
	 "<td><b>TOTAL</b></td>"
	 "<td><b>%ld</b></td>"
	 "<td><b>%ld</b></td>"
	 "<td><b>%ld</b></td>"
	 "<td><b>%ld</b></td>"
	 "<td><b>%ld</b></td>"
	 "<td><b>100.0</b></td>"
	 "</tr>\n",
	 t_files, t_code, t_comment, t_blank, grand_total);

	printf("</tbody>\n</table>\n");

	if (show_files && n_files > 0) {
		printf(
		 "<br>\n"
		 "<table>\n"
		 "<thead>\n"
		 "<tr>\n"
		 "<th>Path</th>\n"
		 "<th>Code</th>\n"
		 "<th>Comment</th>\n"
		 "<th>Blank</th>\n"
		 "<th>Total</th>\n"
		 "</tr>\n"
		 "</thead>\n"
		 "<tbody>\n");

		for (int i = 0; i < n_files; i++) {
			const char* path = LOC__FR_PATH(files_v, i);

			long code = LOC__FR_CODE(files_v, i);
			long comment = LOC__FR_COMMENT(files_v, i);
			long blank = LOC__FR_BLANK(files_v, i);

			long total = code + comment + blank;

			loc__html_escape(path ? path : "", esc, sizeof(esc));

			printf(
			 "<tr>"
			 "<td>%s</td>"
			 "<td>%ld</td>"
			 "<td>%ld</td>"
			 "<td>%ld</td>"
			 "<td>%ld</td>"
			 "</tr>\n",
			 esc, code, comment, blank, total);
		}

		printf("</tbody>\n</table>\n");
	}

	printf("%s", LOC_HTML_FOOTER);

	free(sums);

#undef MAX_SUMS_HTML
}

/*
 * SQL formatter
 */

static void loc_print_sql(const void* files_v, int n_files, const void* langs_v,
 int n_langs, bool show_files)
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
			const char* lang = (li >= 0 && li < n_langs) ?
			 LOC__LANG_NAME(langs_v, li) :
			 "(unknown)";

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
 * TTY formatter
 * ═══════════════════════════════════════════════════════════════════════════
 */

static inline void loc_print_terminal(const void* files_v, int n_files,
 const void* langs_v, int n_langs, bool show_files, bool verbose)
{
#define MAX_SUMS_TERM 1024

	if (n_files == 0) {
		printf("mini-loc: no files processed.\n");
		return;
	}

	LocLangSum* sums = calloc(MAX_SUMS_TERM, sizeof(LocLangSum));

	if (!sums) {
		return;
	}

	long t_files = 0;
	long t_code = 0;
	long t_comment = 0;
	long t_blank = 0;

	int n_sums = loc__build_sums(files_v, n_files, n_langs, sums, MAX_SUMS_TERM,
	 &t_files, &t_code, &t_comment, &t_blank);

	long grand_total = t_code + t_comment + t_blank;

	if (show_files) {
		printf("\n%sPer-File Results%s\n\n", LOC_TERM_CYAN, LOC_TERM_RESET);

		if (verbose) {
			printf("%-45s %-10s %9s %9s %9s %9s\n", "File", "Ext", "Code",
			 "Comment", "Blank", "Total");
		} else {
			printf("%-55s %9s %9s %9s %9s\n", "File", "Code", "Comment",
			 "Blank", "Total");
		}

		for (int i = 0; i < n_files; i++) {
			long code = LOC__FR_CODE(files_v, i);
			long comment = LOC__FR_COMMENT(files_v, i);
			long blank = LOC__FR_BLANK(files_v, i);

			long total = code + comment + blank;

			if (verbose) {
				printf("%-45s %-10s %9ld %9ld %9ld %9ld\n",
				 LOC__FR_PATH(files_v, i),
				 LOC__FR_EXT(files_v, i) ? LOC__FR_EXT(files_v, i) : "", code,
				 comment, blank, total);
			} else {
				printf("%-55s %9ld %9ld %9ld %9ld\n", LOC__FR_PATH(files_v, i),
				 code, comment, blank, total);
			}
		}

		printf("\n");
	}

	printf("\n%sLanguage Summary%s\n\n", LOC_TERM_CYAN, LOC_TERM_RESET);

	printf("%-22s %7s %10s %7s %10s %10s %10s\n", "Language", "Files", "Code",
	 "Pct", "Comment", "Blank", "Total");

	for (int i = 0; i < n_sums; i++) {
		long total =
		 sums[i].counts.code + sums[i].counts.comment + sums[i].counts.blank;

		double pct = (grand_total > 0) ?
		 (100.0 * (double) total / (double) grand_total) :
		 0.0;

		const char* name =
		 (sums[i].lang_idx >= 0 && sums[i].lang_idx < n_langs) ?
		 LOC__LANG_NAME(langs_v, sums[i].lang_idx) :
		 "(unknown)";

		printf(
		 "%-22s %7d "
		 "%s%10ld%s "
		 "%6.1f%% "
		 "%s%10ld%s "
		 "%s%10ld%s "
		 "%10ld\n",
		 name, sums[i].files, LOC_TERM_GREEN, sums[i].counts.code,
		 LOC_TERM_RESET, pct, LOC_TERM_YELLOW, sums[i].counts.comment,
		 LOC_TERM_RESET, LOC_TERM_GRAY, sums[i].counts.blank, LOC_TERM_RESET,
		 total);
	}

	printf("%-22s %7ld %10ld %6.1f%% %10ld %10ld %10ld\n", "TOTAL", t_files,
	 t_code, 100.0, t_comment, t_blank, grand_total);

	free(sums);

#undef MAX_SUMS_TERM
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
		loc_print_terminal(files, n_files, langs, n_langs, show_files, verbose);
		break;

	default:
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
#undef LOC_TERM_RESET
#undef LOC_TERM_CYAN
#undef LOC_TERM_GREEN
#undef LOC_TERM_YELLOW
#undef LOC_TERM_GRAY
#undef LOC_HTML_HEADER
#undef LOC_HTML_FOOTER

#endif /* LOC_OUTPUT_H */
