/*
 * mini-loc — a minimal lines-of-code counter
 * Counts code, comment, and blank lines per file/language.
 * Language definitions loaded from languages.json (no external deps).
 */
#define OFFSET 700
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <sys/stat.h>

#include "include/languages_data.h"
#include "include/minicli.h"
#include "include/set.h"

#define MAX_LANGS 512
#define MAX_EXT_LEN 32
#define MAX_COMMENT_LEN 16
#define MAX_LINE_COMMENTS 8
#define MAX_BLOCK_COMMENTS 8
#define MAX_LANG_NAME_LEN 32
#define MAX_EXTENSIONS 32
#define MAX_FILES 65536
#define PATH_BUF 4096
#define LINE_BUF 65536
#define MAX_FILE_SIZE (1024L * 1024L)

#define COL_LANG 20
#define COL_FILES 6
#define COL_NUM 9
#define COL_PCT 6

#define ANSI_BOLD "\033[1m"
#define ANSI_CYAN "\033[36m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_GRAY "\033[90m"
#define ANSI_RESET "\033[0m"

typedef struct {
	size_t line_comment_lens[MAX_LINE_COMMENTS];
	size_t block_start_lens[MAX_BLOCK_COMMENTS];
	size_t block_end_lens[MAX_BLOCK_COMMENTS];
	int n_extensions;
	int n_line_comments;
	int n_block_comments;
	bool data_only;
	char name[MAX_LANG_NAME_LEN];
	char line_comments[MAX_LINE_COMMENTS][MAX_COMMENT_LEN];
	char block_start[MAX_BLOCK_COMMENTS][MAX_COMMENT_LEN];
	char block_end[MAX_BLOCK_COMMENTS][MAX_COMMENT_LEN];
	char extensions[MAX_EXTENSIONS][MAX_EXT_LEN];
} Language;

typedef struct {
	long code;
	long comment;
	long blank;
} Counts;

typedef struct {
	char* path;
	char* ext;
	int lang_idx;
	Counts counts;
} FileResult;

/*
 * ExtEntry: maps a file extension (or bare filename) to a language index.
 * The table is sorted and searched with bsearch for O(log n) lookup.
 */
typedef struct {
	char ext[MAX_EXT_LEN];
	int lang_idx;
} ExtEntry;

static Language g_langs[MAX_LANGS];
static int g_n_langs = 0;

/* Sorted extension->language table, built once after load_languages. */
static ExtEntry g_ext_table[MAX_LANGS * MAX_EXTENSIONS];
static int g_n_ext_entries = 0;

/*
 * g_ignored_set: O(1) hash-set for ignored extensions (e.g. ".gz", ".png").
 * Replaces the old linear-scan over g_ignored_exts[].
 */
static SimpleSet g_ignored_set;
static bool g_ignored_set_ready = false;

static FileResult* g_files = NULL;
static int g_n_files = 0;
static int g_files_capacity = 0;

static bool g_recurse = false;
static bool g_show_files = false;
static bool g_list_unknown = false;
static bool g_verbose = false;
static char* g_filter = NULL;

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

static void load_languages(const unsigned char* data, size_t len, bool append);
static void build_lookup_table(void);

static void load_languages(const unsigned char* data, size_t len, bool append)
{
	(void) len;
	if (!append) {
		g_n_langs = 0;
		/* Reinitialise the ignored-extension set for a fresh load. */
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
							/* Store in hash set for O(1) lookup later. */
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
									/*
									 * Normal case: each block comment is a
									 * two-element array ["start", "end"].
									 */
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
					temp.line_comment_lens[i] = strlen(temp.line_comments[i]);
				}
				for (int i = 0; i < temp.n_block_comments; i++) {
					temp.block_start_lens[i] = strlen(temp.block_start[i]);
					temp.block_end_lens[i] = strlen(temp.block_end[i]);
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

/* -------------------------------------------------------------------------
 * build_lookup_table
 *
 * Called once after every load_languages() invocation.  Populates a sorted
 * ExtEntry array so find_language() can use bsearch (O(log n)) instead of
 * the original O(langs * extensions) double linear scan.
 * ------------------------------------------------------------------------- */
static int ext_entry_cmp(const void* a, const void* b)
{
	return strcasecmp(((const ExtEntry*) a)->ext, ((const ExtEntry*) b)->ext);
}

static void build_lookup_table(void)
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

typedef struct {
	const char* path;
	const char* ext;
} LangLookupParams;

/*
 * ext_cmp_str: comparator for bsearch() that takes a raw C string as the key
 * instead of a full ExtEntry.  This lets find_language() pass the extension
 * pointer directly, avoiding the memset + strncpy into a temporary ExtEntry
 * that the old implementation required.
 */
static int ext_cmp_str(const void* key, const void* entry)
{
	return strcasecmp((const char*) key, ((const ExtEntry*) entry)->ext);
}

static int find_language(LangLookupParams params)
{
	const char* search_key;
	const char* base = NULL; /* only used for the no-extension path */

	if (!params.ext) {
		/* No extension: match against the bare filename (e.g. Dockerfile). */
		base = strrchr(params.path, '/');
		search_key = base ? base + 1 : params.path;
	} else {
		/* Skip the leading dot so "c" matches the table entry "c". */
		search_key = params.ext + 1;
	}

	const ExtEntry* found = (const ExtEntry*) bsearch(search_key, g_ext_table,
	 (size_t) g_n_ext_entries, sizeof(ExtEntry), ext_cmp_str);
	return found ? found->lang_idx : -1;
}

static inline bool is_space(char c)
{
	static const bool space_table[256] = {[' '] = true,
	    ['\t'] = true,
	    ['\n'] = true,
	    ['\r'] = true,
	    ['\v'] = true,
	    ['\f'] = true};
	return space_table[(unsigned char) c];
}

/*
 * scan_for_end: scan [p, line_end) for a short block-end marker.
 * Uses memchr on the first character and memcmp for the rest — much cheaper
 * than strstr.  Bounded by line_end so we never scan past the current line.
 */
static bool scan_for_end(const char* p, const char* line_end, const char* end,
 size_t end_len)
{
	char first = end[0];
	while (p < line_end) {
		const char* found = (const char*) memchr(p, (unsigned char) first,
		 (size_t) (line_end - p));
		if (!found) {
			return false;
		}
		/* Need end_len bytes starting at found; check they fit. */
		if ((size_t) (line_end - found) >= end_len &&
		 memcmp(found, end, end_len) == 0) {
			return true;
		}
		p = found + 1;
	}
	return false;
}

static Counts count_file(const char* path, int lang_idx)
{
	Counts c = {0, 0, 0};

	FILE* f = fopen(path, "rb");
	if (!f) {
		return c;
	}

	/* Read the entire file in one syscall into a heap buffer. */
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return c;
	}
	long file_len = ftell(f);
	if (file_len < 0) {
		fclose(f);
		return c;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return c;
	}

	char* buf = (char*) malloc((size_t) file_len + 1);
	if (!buf) {
		fclose(f);
		return c;
	}
	size_t nread = fread(buf, 1, (size_t) file_len, f);
	fclose(f);
	/* fread() returns at most the requested count; assert gives the static
	 * analyzer a provable upper bound so it can verify buf[nread] is safe. */
	assert(nread <= (size_t) file_len);
	buf[nread] = '\0';

	Language* l = (lang_idx >= 0) ? &g_langs[lang_idx] : NULL;
	bool in_block = false;
	int block_idx = -1;

	char* cur = buf;
	char* file_end = buf + nread;

	while (cur < file_end) {
		/* Find end of this line. */
		char* lf = (char*) memchr(cur, '\n', (size_t) (file_end - cur));
		char* line_end = lf ? lf : file_end;

		/* Temporarily NUL-terminate so string functions work on this line. */
		char saved = *line_end;
		*line_end = '\0';

		if (l == NULL) {
			/* Unknown language: count everything as code. */
			c.code++;
		} else {
			/* Skip leading whitespace. */
			char* p = cur;
			while (p < line_end && is_space(*p)) {
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
					/* Line-comment check with first-char pre-filter. */
					for (int i = 0; i < l->n_line_comments; i++) {
						if (p[0] == l->line_comments[i][0] &&
						 strncmp(p, l->line_comments[i],
						  l->line_comment_lens[i]) == 0) {
							is_comment = true;
							break;
						}
					}

					/* Block-comment open check with first-char pre-filter. */
					if (!is_comment) {
						for (int i = 0; i < l->n_block_comments; i++) {
							if (p[0] == l->block_start[i][0] &&
							 strncmp(p, l->block_start[i],
							  l->block_start_lens[i]) == 0) {
								is_comment = true;
								if (!scan_for_end(p + l->block_start_lens[i],
								     line_end, l->block_end[i],
								     l->block_end_lens[i])) {
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

		/* Restore the byte we stomped and advance past the newline. */
		*line_end = saved;
		cur = lf ? lf + 1 : file_end;
	}

	free(buf);
	return c;
}

static void walk_dir(const char* path);

static bool is_ignored_extension(const char* ext)
{
	if (!ext) {
		return false;
	}
	/* O(1) hash-set lookup — replaces the original O(n) linear scan. */
	return set_contains_str(&g_ignored_set, ext) == SET_TRUE;
}

/*
 * process_file: handle a path that is already known to be a regular file.
 * Called from both process_path (command-line argument) and walk_dir
 * (when d_type == DT_REG), so the stat/lstat classification is done by the
 * caller and does not need to be repeated here.
 */
static void process_file(const char* path)
{
	const char* ext = strrchr(path, '.');

	if (is_ignored_extension(ext)) {
		return;
	}
	if (g_n_files >= g_files_capacity) {
		g_files_capacity = g_files_capacity == 0 ? 1024 : g_files_capacity * 2;
		FileResult* temp = (FileResult*) realloc(g_files,
		 sizeof(FileResult) * (size_t) g_files_capacity);
		if (!temp) {
			return;
		}
		g_files = temp;
	}

	int li = find_language((LangLookupParams) {path, ext});
	if (li == -1 && !g_list_unknown) {
		return;
	}
	FileResult* fr = &g_files[g_n_files++];
	fr->path = strdup(path);
	fr->ext = ext ? strdup(ext) : NULL;
	fr->lang_idx = li;
	fr->counts = count_file(path, li);
}

static void process_path(const char* path)
{
	struct stat st;
	/*
	 * BUG FIX: use lstat() instead of stat().
	 * stat() follows symlinks, so a symlink pointing to a parent directory
	 * causes infinite mutual recursion between process_path and walk_dir.
	 * lstat() reports the symlink itself; we skip it unconditionally.
	 */
	if (lstat(path, &st) != 0) {
		return;
	}
	if (S_ISLNK(st.st_mode)) {
		return;
	}
	if (S_ISDIR(st.st_mode)) {
		if (g_recurse) {
			walk_dir(path);
		} else {
			fprintf(stderr,
			 "mini-loc: '%s' is a directory (use -r to recurse)\n", path);
		}
		return;
	}
	if (!S_ISREG(st.st_mode)) {
		return;
	}
	process_file(path);
}

static void walk_dir(const char* path)
{
	DIR* d = opendir(path);
	if (!d) {
		return;
	}
	struct dirent* entry;
	while ((entry = readdir(d))) {
		if (entry->d_name[0] == '.') {
			/*
			 * Skips ".", "..", and all hidden entries (.git, .svn, .hg).
			 * Hidden directories can contain symlink loops back to parent
			 * directories — a source of infinite recursion.
			 */
			continue;
		}
		char sub[PATH_BUF];
		snprintf(sub, sizeof(sub), "%s/%s", path, entry->d_name);

		/*
		 * Use d_type to classify the entry without an lstat() syscall.
		 * This saves one syscall per file — on large trees (e.g. Linux
		 * kernel at ~74k files) this eliminates tens of thousands of
		 * syscalls.  DT_UNKNOWN is a fallback for file systems (e.g.
		 * some network mounts) that do not populate d_type; in that case
		 * we fall back to process_path() which calls lstat().
		 */
		switch (entry->d_type) {
		case DT_REG:
			process_file(sub);
			break;
		case DT_DIR:
			if (g_recurse) {
				walk_dir(sub);
			}
			break;
		case DT_LNK:
			/* Symlinks are always skipped to prevent loops. */
			break;
		case DT_UNKNOWN:
			/* Fall back to lstat() for file systems without d_type. */
			process_path(sub);
			break;
		default:
			/* Ignore devices, sockets, FIFOs, etc. */
			break;
		}
	}
	closedir(d);
}

typedef struct {
	int lang_idx;
	int files;
	Counts counts;
} LangSum;

/* Comparator: sort by total lines descending for the summary table. */
static int lang_sum_cmp(const void* lang_sum_a, const void* lang_sum_b)
{
	const LangSum* la = (const LangSum*) lang_sum_a;
	const LangSum* lb = (const LangSum*) lang_sum_b;
	long total_a = la->counts.code + la->counts.comment + la->counts.blank;
	long total_b = lb->counts.code + lb->counts.comment + lb->counts.blank;
	if (total_b > total_a) {
		return 1;
	}
	if (total_b < total_a) {
		return -1;
	}
	return 0;
}

static void print_report(void)
{
	if (g_n_files == 0) {
		printf("mini-loc: no files processed.\n");
		return;
	}

	if (g_show_files) {
		printf("\n %sPer-File Results%s \n", ANSI_CYAN, ANSI_RESET);
		if (g_verbose) {
			printf("%-45s %-10s %9s %9s %9s %9s\n", "File", "Ext", "Code",
			 "Comment", "Blank", "Total");
			printf("-----------------------------------------------------------"
			       "----"
			       "-------------------------------------------\n");
			for (int i = 0; i < g_n_files; i++) {
				FileResult* fr = &g_files[i];
				long total =
				 fr->counts.code + fr->counts.comment + fr->counts.blank;
				printf("%-45s %-10s %9ld %9ld %9ld %9ld\n", fr->path,
				 fr->ext ? fr->ext : "", fr->counts.code, fr->counts.comment,
				 fr->counts.blank, total);
			}
		} else {
			printf("%-55s %9s %9s %9s %9s\n", "File", "Code", "Comment",
			 "Blank", "Total");
			printf("-----------------------------------------------------------"
			       "----"
			       "---------------------------\n");
			for (int i = 0; i < g_n_files; i++) {
				FileResult* fr = &g_files[i];
				long total =
				 fr->counts.code + fr->counts.comment + fr->counts.blank;
				printf("%-55s %9ld %9ld %9ld %9ld\n", fr->path, fr->counts.code,
				 fr->counts.comment, fr->counts.blank, total);
			}
		}
		printf("\n");
	}

	LangSum sums[MAX_LANGS + 1];
	int n_sums = 0;
	int lang_to_sum_idx[MAX_LANGS + 1];
	memset(lang_to_sum_idx, -1, sizeof(lang_to_sum_idx));

	for (int i = 0; i < g_n_files; i++) {
		int li = g_files[i].lang_idx;
		int map_idx = li + 1; /* Map -1 to 0, 0 to 1, etc. */
		int found = lang_to_sum_idx[map_idx];
		if (found == -1) {
			found = n_sums++;
			sums[found].lang_idx = li;
			sums[found].files = 0;
			sums[found].counts = (Counts) {0, 0, 0};
			lang_to_sum_idx[map_idx] = found;
		}
		sums[found].files++;
		sums[found].counts.code += g_files[i].counts.code;
		sums[found].counts.comment += g_files[i].counts.comment;
		sums[found].counts.blank += g_files[i].counts.blank;
	}

	long t_files = 0, t_code = 0, t_comm = 0, t_blank = 0;
	for (int i = 0; i < n_sums; i++) {
		t_files += sums[i].files;
		t_code += sums[i].counts.code;
		t_comm += sums[i].counts.comment;
		t_blank += sums[i].counts.blank;
	}
	long grand_total = t_code + t_comm + t_blank;

	qsort(sums, (size_t) n_sums, sizeof(LangSum), lang_sum_cmp);

	printf("\n %sLanguage Summary%s \n", ANSI_CYAN, ANSI_RESET);
	printf("%-22s %7s %10s %7s %10s %10s %10s\n", "Language", "Files", "Code",
	 "Pct", "Comment", "Blank", "Total");
	printf("-------------------------------------------------------------------"
	       "-------------\n");

	for (int i = 0; i < n_sums; i++) {
		long total =
		 sums[i].counts.code + sums[i].counts.comment + sums[i].counts.blank;
		const char* name = (sums[i].lang_idx == -1) ?
		 "(unknown)" :
		 g_langs[sums[i].lang_idx].name;
		double pct = (grand_total > 0) ?
		 (100.0 * (double) total / (double) grand_total) :
		 0.0;
		printf("%-22s %7d %s%10ld%s %7.1f%% %s%10ld%s %s%10ld%s %10ld\n", name,
		 sums[i].files, ANSI_GREEN, sums[i].counts.code, ANSI_RESET, pct,
		 ANSI_YELLOW, sums[i].counts.comment, ANSI_RESET, ANSI_GRAY,
		 sums[i].counts.blank, ANSI_RESET, total);
	}
	printf("-------------------------------------------------------------------"
	       "-------------\n");
	printf("%-22s %7ld %10ld %7.1f%% %10ld %10ld %10ld\n", "TOTAL", t_files,
	 t_code, 100.0, t_comm, t_blank, grand_total);

	if (grand_total > 0) {
		double c_pct = 100.0 * (double) t_code / (double) grand_total;
		double m_pct = 100.0 * (double) t_comm / (double) grand_total;
		double b_pct = 100.0 * (double) t_blank / (double) grand_total;
		printf("\nBreakdown: %sCode %.1f%%%s | %sComment %.1f%%%s | %sBlank "
		       "%.1f%%%s\n",
		 ANSI_GREEN, c_pct, ANSI_RESET, ANSI_YELLOW, m_pct, ANSI_RESET,
		 ANSI_GRAY, b_pct, ANSI_RESET);
	}
	putchar('\n');
}

static void cb_verbose(int argc, char** argv, void* user_data)
{
	(void) argc;
	(void) argv;
	(void) user_data;
	g_verbose = true;
}

static void cb_recurse(int argc, char** argv, void* user_data)
{
	(void) argc;
	(void) argv;
	(void) user_data;
	g_recurse = true;
}

static void cb_files(int argc, char** argv, void* user_data)
{
	(void) argc;
	(void) argv;
	(void) user_data;
	g_show_files = true;
}

static void cb_list_unknown(int argc, char** argv, void* user_data)
{
	(void) argc;
	(void) argv;
	(void) user_data;
	g_list_unknown = true;
}

static void cb_filter(int argc, char** argv, void* user_data)
{
	if (argc > 0) {
		g_filter = argv[0];
	}
	(void) user_data;
}

static void cb_lang_file(int argc, char** argv, void* user_data)
{
	if (argc > 0) {
		FILE* f = fopen(argv[0], "rb");
		if (f) {
			fseek(f, 0, SEEK_END);
			long len = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (len > 0 && len < MAX_FILE_SIZE) {
				unsigned char* buf = (unsigned char*) malloc((size_t) len + 1);
				if (buf != NULL) {
					if (fread(buf, 1, (size_t) len, f) == (size_t) len) {
						if ((size_t) len < (size_t) len + 1) {
							buf[(size_t) len] = 0;
						}
						load_languages(buf, (size_t) len, false);
						build_lookup_table();
					}
					free(buf);
				}
			}
			fclose(f);
		} else {
			fprintf(stderr,
			 "mini-loc: failed to load language definitions from '%s'\n",
			 argv[0]);
		}
	}
	(void) user_data;
}

static void cb_append(int argc, char** argv, void* user_data)
{
	if (argc > 0) {
		FILE* f = fopen(argv[0], "rb");
		if (f) {
			fseek(f, 0, SEEK_END);
			long len = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (len > 0 && len < MAX_FILE_SIZE) {
				unsigned char* buf = (unsigned char*) malloc((size_t) len + 1);
				if (buf != NULL) {
					if (fread(buf, 1, (size_t) len, f) == (size_t) len) {
						if ((size_t) len < (size_t) len + 1) {
							buf[(size_t) len] = 0;
						}
						load_languages(buf, (size_t) len, true);
						build_lookup_table();
					}
					free(buf);
				}
			}
			fclose(f);
		}
	}
	(void) user_data;
}

int main(int argc, char** argv)
{
	set_init(&g_ignored_set);
	g_ignored_set_ready = true;
	load_languages(languages_json, languages_json_len, false);
	build_lookup_table();

	CliParser parser;
	cli_init(&parser,
	 (CliInitParams) {
	     "mini-loc", "Counts lines of code, comments, and blanks."});
	cli_add_argument(&parser,
	 (CliArgument) {
	     "--recurse", "-r", "Recurse into directories", cb_recurse, NULL});
	cli_add_argument(&parser,
	 (CliArgument) {"--files", "-f", "Show per-file results", cb_files, NULL});
	cli_add_argument(&parser,
	 (CliArgument) {
	     "--lang-file", "-l", "Language definition file", cb_lang_file, NULL});
	cli_add_argument(&parser,
	 (CliArgument) {
	     "--append", "-a", "Append language definitions", cb_append, NULL});
	cli_add_argument(&parser,
	 (CliArgument) {
	     "--list-unknown", NULL, "List unknown files", cb_list_unknown, NULL});
	cli_add_argument(&parser,
	 (CliArgument) {
	     "--verbose", NULL, "Show file extensions", cb_verbose, NULL});
	cli_add_argument(&parser,
	 (CliArgument) {"--filter", NULL, "Filter output: code, comment, or blank",
	     cb_filter, NULL});

	if (argc < 2) {
		process_path(".");
	} else {
		/* Parse flags */
		for (int i = 1; i < argc; i++) {
			if (argv[i][0] == '-') {
				for (size_t j = 0; j < parser.arg_count; j++) {
					if (strcmp(argv[i], parser.registered_args[j].name) == 0 ||
					 (parser.registered_args[j].shorthand &&
					  strcmp(argv[i], parser.registered_args[j].shorthand) ==
					   0)) {
						int n_args = 0;
						if (strcmp(argv[i], "-l") == 0 ||
						 strcmp(argv[i], "--lang-file") == 0 ||
						 strcmp(argv[i], "-a") == 0 ||
						 strcmp(argv[i], "--append") == 0 ||
						 strcmp(argv[i], "--filter") == 0) {
							n_args = 1;
						}
						parser.registered_args[j].callback(n_args,
						 (n_args > 0) ? &argv[i + 1] : NULL,
						 parser.registered_args[j].user_data);
						if (n_args > 0) {
							i++;
						}
						break;
					}
				}
			}
		}

		/* Process paths */
		bool any_file = false;
		for (int i = 1; i < argc; i++) {
			if (argv[i][0] == '-') {
				if (strcmp(argv[i], "-l") == 0 ||
				 strcmp(argv[i], "--lang-file") == 0 ||
				 strcmp(argv[i], "-a") == 0 ||
				 strcmp(argv[i], "--append") == 0 ||
				 strcmp(argv[i], "--filter") == 0) {
					i++;
				}
				continue;
			}
			process_path(argv[i]);
			any_file = true;
		}
		if (!any_file && g_n_files == 0) {
			process_path(".");
		}
	}
	print_report();
	cli_destroy(&parser);
	set_destroy(&g_ignored_set);
	return 0;
}