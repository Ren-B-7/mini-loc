/*
 * mini-loc — a minimal lines-of-code counter
 * Counts code, comment, and blank lines per file/language.
 * Language definitions loaded from languages.json (no external deps).
 */
#define OFFSET 700
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <sys/stat.h>
#include <unistd.h>  /* sysconf */

#include "include/languages_data.h"
#include "include/minicli.h"
#include "include/set.h"

/* -------------------------------------------------------------------------
 * Compile-time constants
 * ------------------------------------------------------------------------- */
#define MAX_LANGS 512
#define MAX_EXT_LEN 32
#define MAX_COMMENT_LEN 16
#define MAX_LINE_COMMENTS 8
#define MAX_BLOCK_COMMENTS 8
#define MAX_LANG_NAME_LEN 32
#define MAX_EXTENSIONS 32
#define PATH_BUF 4096
#define MAX_FILE_SIZE (1024L * 1024L)

/* Work-queue / thread-pool tuning */
#define QUEUE_INIT_CAP 4096 /* initial work-queue slot count (power of 2) */
#define MAX_THREADS 64      /* hard upper bound on pool size              */
#define LOCAL_INIT_CAP 1024 /* initial per-thread FileResult capacity     */

/* Report column widths */
#define COL_LANG 20
#define COL_FILES 6
#define COL_NUM 9
#define COL_PCT 6

/* ANSI colours */
#define ANSI_BOLD "\033[1m"
#define ANSI_CYAN "\033[36m"
#define ANSI_GREEN "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_GRAY "\033[90m"
#define ANSI_RESET "\033[0m"

/* -------------------------------------------------------------------------
 * Data types
 * ------------------------------------------------------------------------- */
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

/* Extension → language index: sorted, searched with bsearch O(log n). */
typedef struct {
	char ext[MAX_EXT_LEN];
	int lang_idx;
} ExtEntry;

typedef struct {
	const char* path;
	const char* ext;
} LangLookupParams;

/* =========================================================================
 * Read-only globals — written once at startup, never mutated afterwards.
 * Workers can read these without any locking.
 * ========================================================================= */
static Language g_langs[MAX_LANGS];
static int g_n_langs = 0;

static ExtEntry g_ext_table[MAX_LANGS * MAX_EXTENSIONS];
static int g_n_ext_entries = 0;

static SimpleSet g_ignored_set;
static bool g_ignored_set_ready = false;

/* =========================================================================
 * CLI option flags — set once during argument parsing, read-only afterwards.
 * ========================================================================= */
static bool g_recurse = false;
static bool g_show_files = false;
static bool g_list_unknown = false;
static bool g_verbose = false;
static char* g_filter = NULL;

/* =========================================================================
 * Work queue
 *
 * A circular buffer of heap-allocated path strings.  The producer (main /
 * walk_dir) pushes; workers pop.  Both ends are protected by a single
 * mutex + condvar pair — contention is low because each work item
 * represents a whole file (the heavy work happens after the pop).
 * ========================================================================= */
typedef struct {
	char** paths;  /* circular buffer of strdup'd paths */
	size_t cap;    /* allocated slots (always power of 2) */
	size_t head;   /* next slot to pop  (consumer index) */
	size_t tail;   /* next slot to push (producer index) */
	size_t count;  /* items currently in the queue */
	bool finished; /* set by producer when walking is done */
	pthread_mutex_t lock;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
} WorkQueue;

/* =========================================================================
 * Per-thread state
 * ========================================================================= */
typedef struct {
	WorkQueue* queue;  /* shared work queue */
	FileResult* files; /* private result array */
	int n_files;       /* used entries */
	int capacity;      /* allocated entries */
} ThreadState;

/* =========================================================================
 * Forward declarations
 * ========================================================================= */
static const char* json_skip_whitespace(const char* p);
static const char* json_read_string(const char* p, char* buf, size_t len);
static const char* json_skip_value(const char* p);
static void load_languages(const unsigned char* data, size_t len, bool append);
static void build_lookup_table(void);
static int find_language(LangLookupParams params);
static bool is_ignored_extension(const char* ext);
static Counts count_file(const char* path, int lang_idx);
static void process_file_local(ThreadState* ts, const char* path);
static void walk_dir(const char* path, WorkQueue* queue);
static void process_path_producer(const char* path, WorkQueue* queue);
static int wq_init(WorkQueue* q, size_t initial_cap);
static void wq_push(WorkQueue* q, char* path);
static char* wq_pop(WorkQueue* q); /* blocks; returns NULL when done */
static void wq_finish(WorkQueue* q);
static void wq_destroy(WorkQueue* q);
static void* worker_thread(void* arg);

/* =========================================================================
 * JSON mini-parser (unchanged from original)
 * ========================================================================= */
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
		while (*p && !strchr(",] \n\r\t}", *p)) {
			p++;
		}
	}
	return p;
}

/* =========================================================================
 * Language loading & lookup (unchanged from original)
 * ========================================================================= */
static void load_languages(const unsigned char* data, size_t len, bool append)
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

static int ext_cmp_str(const void* key, const void* entry)
{
	return strcasecmp((const char*) key, ((const ExtEntry*) entry)->ext);
}

static int find_language(LangLookupParams params)
{
	const char* search_key;
	const char* base = NULL;

	if (!params.ext) {
		base = strrchr(params.path, '/');
		search_key = base ? base + 1 : params.path;
	} else {
		search_key = params.ext + 1; /* skip the leading dot */
	}

	const ExtEntry* found = (const ExtEntry*) bsearch(search_key, g_ext_table,
	 (size_t) g_n_ext_entries, sizeof(ExtEntry), ext_cmp_str);
	return found ? found->lang_idx : -1;
}

/* =========================================================================
 * Core counting (pure — no global writes, safe to call from any thread)
 * ========================================================================= */
static inline bool is_space_char(char c)
{
	static const bool space_table[256] = {
	    [' '] = true,
	    ['\t'] = true,
	    ['\n'] = true,
	    ['\r'] = true,
	    ['\v'] = true,
	    ['\f'] = true,
	};
	return space_table[(unsigned char) c];
}

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
		if ((size_t) (line_end - found) >= end_len &&
		 memcmp(found, end, end_len) == 0) {
			return true;
		}
		p = found + 1;
	}
	return false;
}

static bool is_ignored_extension(const char* ext)
{
	if (!ext) {
		return false;
	}
	return set_contains_str(&g_ignored_set, ext) == SET_TRUE;
}

static Counts count_file(const char* path, int lang_idx)
{
	Counts c = {0, 0, 0};

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
	if (buf_size == 0) {
		fclose(f);
		return c;
	}
	char* buf = (char*) calloc(buf_size, 1);
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

		size_t line_end_off = (size_t) (line_end - buf);
		if (line_end_off >= buf_size) {
			break;
		}

		char saved = buf[line_end_off];
		buf[line_end_off] = '\0';

		if (l == NULL) {
			c.code++;
		} else {
			char* p = cur;
			while (p < line_end && is_space_char(*p)) {
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
						if (p[0] == l->line_comments[i][0] &&
						 strncmp(p, l->line_comments[i],
						  l->line_comment_lens[i]) == 0) {
							is_comment = true;
							break;
						}
					}

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

		buf[line_end_off] = saved;
		cur = lf ? lf + 1 : file_end;
	}

	free(buf);
	return c;
}

/* =========================================================================
 * Per-thread file processing — appends to the thread's private FileResult
 * array.  No locks needed; nothing shared is written here.
 * ========================================================================= */
static void process_file_local(ThreadState* ts, const char* path)
{
	const char* ext = strrchr(path, '.');

	if (is_ignored_extension(ext)) {
		return;
	}

	int li = find_language((LangLookupParams) {path, ext});
	if (li == -1 && !g_list_unknown) {
		return;
	}

	/* Grow the private array if needed. */
	if (ts->n_files >= ts->capacity) {
		int new_cap = ts->capacity * 2;
		FileResult* tmp = (FileResult*) realloc(ts->files,
		 sizeof(FileResult) * (size_t) new_cap);
		if (!tmp) {
			return;
		}
		ts->files = tmp;
		ts->capacity = new_cap;
	}

	FileResult* fr = &ts->files[ts->n_files++];
	fr->path = strdup(path);
	fr->ext = ext ? strdup(ext) : NULL;
	fr->lang_idx = li;
	fr->counts = count_file(path, li);
}

/* =========================================================================
 * Work queue implementation
 * ========================================================================= */
static int wq_init(WorkQueue* q, size_t initial_cap)
{
	q->paths = (char**) malloc(sizeof(char*) * initial_cap);
	if (!q->paths) {
		return -1;
	}
	q->cap = initial_cap;
	q->head = 0;
	q->tail = 0;
	q->count = 0;
	q->finished = false;

	if (pthread_mutex_init(&q->lock, NULL) != 0) {
		free((void*) q->paths);
		return -1;
	}
	if (pthread_cond_init(&q->not_empty, NULL) != 0) {
		pthread_mutex_destroy(&q->lock);
		free((void*) q->paths);
		return -1;
	}
	if (pthread_cond_init(&q->not_full, NULL) != 0) {
		pthread_cond_destroy(&q->not_empty);
		pthread_mutex_destroy(&q->lock);
		free((void*) q->paths);
		return -1;
	}
	return 0;
}

static void wq_push(WorkQueue* q, char* path)
{
	pthread_mutex_lock(&q->lock);

	/* Grow queue if full — double capacity. */
	while (q->count == q->cap) {
		/*
		 * Rather than blocking the producer (which would stall directory
		 * walking), we grow the buffer inline under the lock.  The queue
		 * rarely exceeds its initial capacity for typical trees; for
		 * extreme cases (Linux kernel) the one or two doublings are cheap
		 * relative to the I/O work waiting in the queue.
		 */
		size_t new_cap = q->cap * 2;
		char** new_buf = (char**) malloc(sizeof(char*) * new_cap);
		if (!new_buf) {
			/*
			 * Allocation failed: fall back to blocking on not_full and
			 * wait for workers to drain entries.  This path is extremely
			 * unlikely — it requires both a huge queue AND an OOM
			 * condition simultaneously.
			 */
			pthread_cond_wait(&q->not_full, &q->lock);
			continue;
		}
		/* Linearise the circular buffer into the new allocation. */
		for (size_t i = 0; i < q->count; i++) {
			new_buf[i] = q->paths[(q->head + i) % q->cap];
		}
		free((void*) q->paths);
		q->paths = new_buf;
		q->head = 0;
		q->tail = q->count;
		q->cap = new_cap;
	}

	q->paths[q->tail] = path;
	q->tail = (q->tail + 1) % q->cap;
	q->count++;

	pthread_cond_signal(&q->not_empty);
	pthread_mutex_unlock(&q->lock);
}

/*
 * wq_pop: blocks until either a path is available or the queue is finished
 * and empty.  Returns NULL (sentinel) when workers should exit.
 */
static char* wq_pop(WorkQueue* q)
{
	pthread_mutex_lock(&q->lock);

	while (q->count == 0 && !q->finished) {
		pthread_cond_wait(&q->not_empty, &q->lock);
	}

	if (q->count == 0) {
		/* finished == true and empty: signal other sleeping workers, then exit.
		 */
		pthread_cond_broadcast(&q->not_empty);
		pthread_mutex_unlock(&q->lock);
		return NULL;
	}

	char* path = q->paths[q->head];
	q->head = (q->head + 1) % q->cap;
	q->count--;

	pthread_cond_signal(&q->not_full);
	pthread_mutex_unlock(&q->lock);
	return path;
}

static void wq_finish(WorkQueue* q)
{
	pthread_mutex_lock(&q->lock);
	q->finished = true;
	pthread_cond_broadcast(&q->not_empty);
	pthread_mutex_unlock(&q->lock);
}

static void wq_destroy(WorkQueue* q)
{
	free((void*) q->paths);
	pthread_cond_destroy(&q->not_full);
	pthread_cond_destroy(&q->not_empty);
	pthread_mutex_destroy(&q->lock);
}

/* =========================================================================
 * Worker thread entry point
 * ========================================================================= */
static void* worker_thread(void* arg)
{
	ThreadState* ts = (ThreadState*) arg;
	for (;;) {
		char* path = wq_pop(ts->queue);
		if (!path) {
			break; /* queue drained and finished */
		}
		process_file_local(ts, path);
		free(path);
	}
	return NULL;
}

/* =========================================================================
 * Directory walking — runs on the main thread and pushes paths onto the
 * work queue.  Subdirectory recursion still happens inline here (the
 * opendir/readdir calls are fast; the expensive work is count_file which
 * happens in workers).
 * ========================================================================= */
static void walk_dir(const char* path, WorkQueue* queue)
{
	DIR* d = opendir(path);
	if (!d) {
		return;
	}
	struct dirent* entry;
	while ((entry = readdir(d))) {
		if (entry->d_name[0] == '.') {
			continue;
		}
		char sub[PATH_BUF];
		snprintf(sub, sizeof(sub), "%s/%s", path, entry->d_name);

		switch (entry->d_type) {
		case DT_REG:
			wq_push(queue, strdup(sub));
			break;
		case DT_DIR:
			if (g_recurse) {
				walk_dir(sub, queue);
			}
			break;
		case DT_LNK:
			/* Always skip symlinks to prevent loops. */
			break;
		case DT_UNKNOWN: {
			/* File system doesn't provide d_type; fall back to lstat. */
			struct stat st;
			if (lstat(sub, &st) != 0) {
				break;
			}
			if (S_ISLNK(st.st_mode)) {
				break;
			}
			if (S_ISREG(st.st_mode)) {
				wq_push(queue, strdup(sub));
			} else if (S_ISDIR(st.st_mode) && g_recurse) {
				walk_dir(sub, queue);
			}
			break;
		}
		default:
			break;
		}
	}
	closedir(d);
}

static void process_path_producer(const char* path, WorkQueue* queue)
{
	struct stat st;
	if (lstat(path, &st) != 0) {
		return;
	}
	if (S_ISLNK(st.st_mode)) {
		return;
	}
	if (S_ISDIR(st.st_mode)) {
		if (g_recurse) {
			walk_dir(path, queue);
		} else {
			fprintf(stderr,
			 "mini-loc: '%s' is a directory (use -r to recurse)\n", path);
		}
		return;
	}
	if (!S_ISREG(st.st_mode)) {
		return;
	}
	wq_push(queue, strdup(path));
}

/* =========================================================================
 * Report
 * ========================================================================= */
typedef struct {
	int lang_idx;
	int files;
	Counts counts;
} LangSum;

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

static void print_report(FileResult* files, int n_files)
{
	if (n_files == 0) {
		printf("mini-loc: no files processed.\n");
		return;
	}

	if (g_show_files) {
		printf("\n %sPer-File Results%s \n", ANSI_CYAN, ANSI_RESET);
		if (g_verbose) {
			printf("%-45s %-10s %9s %9s %9s %9s\n", "File", "Ext", "Code",
			 "Comment", "Blank", "Total");
			printf("-----------------------------------------------------------"
			       "-----"
			       "-------------------------------------------\n");
			for (int i = 0; i < n_files; i++) {
				FileResult* fr = &files[i];
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
			       "-----"
			       "---------------------------\n");
			for (int i = 0; i < n_files; i++) {
				FileResult* fr = &files[i];
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

	for (int i = 0; i < n_files; i++) {
		int li = files[i].lang_idx;
		int map_idx = li + 1;
		int found = lang_to_sum_idx[map_idx];
		if (found == -1) {
			found = n_sums++;
			sums[found].lang_idx = li;
			sums[found].files = 0;
			sums[found].counts = (Counts) {0, 0, 0};
			lang_to_sum_idx[map_idx] = found;
		}
		sums[found].files++;
		sums[found].counts.code += files[i].counts.code;
		sums[found].counts.comment += files[i].counts.comment;
		sums[found].counts.blank += files[i].counts.blank;
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

/* =========================================================================
 * CLI callbacks
 * ========================================================================= */
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

/* =========================================================================
 * main
 * ========================================================================= */
int main(int argc, char** argv)
{
	/* --- Language initialisation (single-threaded, before any workers) --- */
	set_init(&g_ignored_set);
	g_ignored_set_ready = true;
	load_languages(languages_json, languages_json_len, false);
	build_lookup_table();

	/* --- CLI setup --- */
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

	/* --- Parse flags (no paths yet) --- */
	if (argc >= 2) {
		for (int i = 1; i < argc; i++) {
			if (argv[i][0] != '-') {
				continue;
			}
			for (size_t j = 0; j < parser.arg_count; j++) {
				if (strcmp(argv[i], parser.registered_args[j].name) == 0 ||
				 (parser.registered_args[j].shorthand &&
				  strcmp(argv[i], parser.registered_args[j].shorthand) == 0)) {
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

	/* --- Determine thread count --- */
	long nproc = sysconf(_SC_NPROCESSORS_ONLN);
	if (nproc < 1) {
		nproc = 1;
	}
	if (nproc > MAX_THREADS) {
		nproc = MAX_THREADS;
	}
	int n_threads = (int) nproc;

	/* --- Initialise work queue --- */
	WorkQueue queue;
	if (wq_init(&queue, QUEUE_INIT_CAP) != 0) {
		fprintf(stderr, "mini-loc: failed to initialise work queue\n");
		cli_destroy(&parser);
		set_destroy(&g_ignored_set);
		return 1;
	}

	/* --- Allocate per-thread state --- */
	ThreadState* states =
	 (ThreadState*) malloc(sizeof(ThreadState) * (size_t) n_threads);
	if (!states) {
		fprintf(stderr, "mini-loc: out of memory\n");
		wq_destroy(&queue);
		cli_destroy(&parser);
		set_destroy(&g_ignored_set);
		return 1;
	}
	for (int i = 0; i < n_threads; i++) {
		states[i].queue = &queue;
		states[i].n_files = 0;
		states[i].capacity = LOCAL_INIT_CAP;
		states[i].files =
		 (FileResult*) malloc(sizeof(FileResult) * (size_t) LOCAL_INIT_CAP);
		if (!states[i].files) {
			/* Clean up already-allocated states */
			for (int k = 0; k < i; k++) {
				free(states[k].files);
			}
			free(states);
			wq_destroy(&queue);
			cli_destroy(&parser);
			set_destroy(&g_ignored_set);
			return 1;
		}
	}

	/* --- Spawn workers --- */
	pthread_t* threads =
	 (pthread_t*) malloc(sizeof(pthread_t) * (size_t) n_threads);
	if (!threads) {
		fprintf(stderr, "mini-loc: out of memory\n");
		for (int i = 0; i < n_threads; i++) {
			free(states[i].files);
		}
		free(states);
		wq_destroy(&queue);
		cli_destroy(&parser);
		set_destroy(&g_ignored_set);
		return 1;
	}
	for (int i = 0; i < n_threads; i++) {
		pthread_create(&threads[i], NULL, worker_thread, &states[i]);
	}

	/* --- Producer: walk paths and push file paths onto the queue --- */
	if (argc < 2) {
		process_path_producer(".", &queue);
	} else {
		bool any_path = false;
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
			process_path_producer(argv[i], &queue);
			any_path = true;
		}
		if (!any_path) {
			process_path_producer(".", &queue);
		}
	}

	/* Signal workers that no more paths are coming. */
	wq_finish(&queue);

	/* --- Join workers --- */
	for (int i = 0; i < n_threads; i++) {
		pthread_join(threads[i], NULL);
	}
	free(threads);

	/* --- Merge per-thread results into one flat array --- */
	int total_files = 0;
	for (int i = 0; i < n_threads; i++) {
		total_files += states[i].n_files;
	}

	FileResult* all_files = NULL;
	if (total_files > 0) {
		all_files =
		 (FileResult*) malloc(sizeof(FileResult) * (size_t) total_files);
		if (!all_files) {
			fprintf(stderr, "mini-loc: out of memory during merge\n");
			/* Fall through to cleanup; report will show 0 files. */
			total_files = 0;
		} else {
			int offset = 0;
			for (int i = 0; i < n_threads; i++) {
				memcpy(&all_files[offset], states[i].files,
				 sizeof(FileResult) * (size_t) states[i].n_files);
				offset += states[i].n_files;
			}
		}
	}

	/* --- Free per-thread arrays (entries now copied into all_files) --- */
	for (int i = 0; i < n_threads; i++) {
		free(states[i].files);
	}
	free(states);

	/* --- Report --- */
	print_report(all_files, total_files);

	/* --- Final cleanup --- */
	if (all_files) {
		for (int i = 0; i < total_files; i++) {
			free(all_files[i].path);
			free(all_files[i].ext);
		}
		free(all_files);
	}

	wq_destroy(&queue);
	cli_destroy(&parser);
	set_destroy(&g_ignored_set);
	return 0;
}
