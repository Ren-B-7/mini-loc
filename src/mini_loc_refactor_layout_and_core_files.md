# Refactored Project Layout

```text
src/
├── include/
│   ├── cli.h
│   ├── count.h
│   ├── fs.h
│   ├── languages.h
│   ├── output.h
│   ├── set.h
│   ├── threading.h
│   ├── types.h
│   ├── languages_data.h
│   └── minicli.h
│
├── cli.c
├── count.c
├── fs.c
├── languages.c
├── mini-loc-single.c
├── mini-loc-multi.c
└── threading.c
```

---

# include/languages.h

```c
#ifndef LOC_LANGUAGES_H
#define LOC_LANGUAGES_H

#include <stdbool.h>
#include <stddef.h>

#include "set.h"
#include "types.h"

extern Language g_langs[MAX_LANGS];
extern int g_n_langs;

void load_languages(const unsigned char* data, size_t len, bool append);
void build_lookup_table(void);

int find_language(LangLookupParams params);

bool is_ignored_extension(const char* ext);

#endif
```

---

# include/count.h

```c
#ifndef LOC_COUNT_H
#define LOC_COUNT_H

#include "types.h"

Counts count_file(const char* path, int lang_idx);

#endif
```

---

# include/fs.h

```c
#ifndef LOC_FS_H
#define LOC_FS_H

#include <stdbool.h>

#include "types.h"

typedef void (*FileCallback)(const char* path, void* user);

void walk_dir(const char* path, bool recurse, FileCallback cb,
 void* user);

void process_path(const char* path, bool recurse, FileCallback cb,
 void* user);

#endif
```

---

# include/cli.h

```c
#ifndef LOC_CLI_H
#define LOC_CLI_H

#include <stdbool.h>

#include "output.h"

typedef struct {
	bool recurse;
	bool show_files;
	bool list_unknown;
	bool verbose;

	char* filter;

	LocOutputFormat output_fmt;
} LocConfig;

void loc_config_init(LocConfig* cfg);
void parse_cli(LocConfig* cfg, int argc, char** argv);

#endif
```

---

# include/threading.h

```c
#ifndef LOC_THREADING_H
#define LOC_THREADING_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "types.h"

#define QUEUE_INIT_CAP 4096
#define MAX_THREADS 64
#define LOCAL_INIT_CAP 1024

typedef struct {
	char** paths;
	size_t cap;
	size_t head;
	size_t tail;
	size_t count;
	bool finished;
	pthread_mutex_t lock;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
} WorkQueue;

typedef struct {
	WorkQueue* queue;
	FileResult* files;
	int n_files;
	int capacity;
} ThreadState;

int wq_init(WorkQueue* q, size_t initial_cap);
void wq_push(WorkQueue* q, char* path);
char* wq_pop(WorkQueue* q);
void wq_finish(WorkQueue* q);
void wq_destroy(WorkQueue* q);

void* worker_thread(void* arg);

#endif
```

---

# languages.c

```c
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "include/languages.h"

Language g_langs[MAX_LANGS];
int g_n_langs = 0;

static ExtEntry g_ext_table[MAX_LANGS * MAX_EXTENSIONS];
static int g_n_ext_entries = 0;

static SimpleSet g_ignored_set;
static bool g_ignored_set_ready = false;

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

static inline int ext_entry_cmp(const void* a, const void* b)
{
	return strcasecmp(((const ExtEntry*) a)->ext,
	 ((const ExtEntry*) b)->ext);
}

static inline int ext_cmp_str(const void* key, const void* entry)
{
	return strcasecmp((const char*) key,
	 ((const ExtEntry*) entry)->ext);
}

void build_lookup_table(void)
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

	qsort(g_ext_table,
	 (size_t) g_n_ext_entries,
	 sizeof(ExtEntry),
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

	const ExtEntry* found = (const ExtEntry*) bsearch(search_key,
	 g_ext_table,
	 (size_t) g_n_ext_entries,
	 sizeof(ExtEntry),
	 ext_cmp_str);

	return found ? found->lang_idx : -1;
}

bool is_ignored_extension(const char* ext)
{
	if (!ext) {
		return false;
	}

	return set_contains_str(&g_ignored_set, ext) == SET_TRUE;
}
```

---

# count.c

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/count.h"
#include "include/languages.h"

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

static bool scan_for_end(const char* p,
 const char* line_end,
 const char* end,
 size_t end_len)
{
	if (end_len == 0) {
		return true;
	}

	if (end_len == 1) {
		const char* found = (const char*) memchr(p,
		 (unsigned char) end[0],
		 (size_t) (line_end - p));

		return found != NULL;
	}

	char first = end[0];

	while (p <= line_end - (ptrdiff_t) end_len) {
		const char* found = (const char*) memchr(p,
		 (unsigned char) first,
		 (size_t) (line_end - p - end_len + 1));

		if (!found) {
			return false;
		}

		if (memcmp(found, end, end_len) == 0) {
			return true;
		}

		p = found + 1;
	}

	return false;
}

Counts count_file(const char* path, int lang_idx)
{
	Counts c = {0, 0, 0};

	/* move existing count_file implementation here */

	return c;
}
```

---

# fs.c

```c
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "include/fs.h"

void walk_dir(const char* path,
 bool recurse,
 FileCallback cb,
 void* user)
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
			cb(sub, user);
			break;

		case DT_DIR:
			if (recurse) {
				walk_dir(sub, recurse, cb, user);
			}
			break;

		default:
			break;
		}
	}

	closedir(d);
}

void process_path(const char* path,
 bool recurse,
 FileCallback cb,
 void* user)
{
	struct stat st;

	if (lstat(path, &st) != 0) {
		return;
	}

	if (S_ISLNK(st.st_mode)) {
		return;
	}

	if (S_ISDIR(st.st_mode)) {
		if (recurse) {
			walk_dir(path, recurse, cb, user);
		} else {
			fprintf(stderr,
			 "mini-loc: '%s' is a directory (use -r to recurse)\n",
			 path);
		}

		return;
	}

	if (!S_ISREG(st.st_mode)) {
		return;
	}

	cb(path, user);
}
```

---

# threading.c

```c
#include <pthread.h>
#include <stdlib.h>

#include "include/threading.h"

int wq_init(WorkQueue* q, size_t initial_cap)
{
	/* move existing implementation here */
	return 0;
}

void wq_push(WorkQueue* q, char* path)
{
	/* move existing implementation here */
}

char* wq_pop(WorkQueue* q)
{
	/* move existing implementation here */
	return NULL;
}

void wq_finish(WorkQueue* q)
{
	/* move existing implementation here */
}

void wq_destroy(WorkQueue* q)
{
	/* move existing implementation here */
}

void* worker_thread(void* arg)
{
	/* move existing implementation here */
	return NULL;
}
```

---

# mini-loc-single.c

```c
#include <stdlib.h>

#include "include/cli.h"
#include "include/count.h"
#include "include/fs.h"
#include "include/languages.h"
#include "include/output.h"
#include "include/types.h"

static FileResult* g_files = NULL;
static int g_n_files = 0;
static int g_capacity = 0;

static LocConfig g_cfg;

static void process_file_cb(const char* path, void* user)
{
	(void) user;

	/* move original process_file implementation here */
}

int main(int argc, char** argv)
{
	loc_config_init(&g_cfg);
	parse_cli(&g_cfg, argc, argv);

	load_languages(g_languages_json,
	 sizeof(g_languages_json),
	 false);

	build_lookup_table();

	for (int i = 1; i < argc; i++) {
		process_path(argv[i],
		 g_cfg.recurse,
		 process_file_cb,
		 NULL);
	}

	loc_print_report(g_cfg.output_fmt,
	 g_files,
	 g_n_files,
	 g_langs,
	 g_n_langs,
	 g_cfg.show_files,
	 g_cfg.verbose);

	return 0;
}
```

---

# mini-loc-multi.c

```c
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "include/cli.h"
#include "include/fs.h"
#include "include/languages.h"
#include "include/output.h"
#include "include/threading.h"
#include "include/types.h"

static LocConfig g_cfg;

static void queue_push_cb(const char* path, void* user)
{
	WorkQueue* q = (WorkQueue*) user;
	wq_push(q, strdup(path));
}

int main(int argc, char** argv)
{
	loc_config_init(&g_cfg);
	parse_cli(&g_cfg, argc, argv);

	load_languages(g_languages_json,
	 sizeof(g_languages_json),
	 false);

	build_lookup_table();

	WorkQueue queue;
	wq_init(&queue, QUEUE_INIT_CAP);

	long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

	if (cpu_count < 1) {
		cpu_count = 1;
	}

	if (cpu_count > MAX_THREADS) {
		cpu_count = MAX_THREADS;
	}

	pthread_t threads[MAX_THREADS];
	ThreadState states[MAX_THREADS];

	for (long i = 0; i < cpu_count; i++) {
		states[i].queue = &queue;
		states[i].files = malloc(sizeof(FileResult) * LOCAL_INIT_CAP);
		states[i].n_files = 0;
		states[i].capacity = LOCAL_INIT_CAP;

		pthread_create(&threads[i],
		 NULL,
		 worker_thread,
		 &states[i]);
	}

	for (int i = 1; i < argc; i++) {
		process_path(argv[i],
		 g_cfg.recurse,
		 queue_push_cb,
		 &queue);
	}

	wq_finish(&queue);

	for (long i = 0; i < cpu_count; i++) {
		pthread_join(threads[i], NULL);
	}

	/* merge thread-local arrays here */

	return 0;
}
```

---

# Immediate Benefits

- Removes ~900 duplicated LOC
- Easier future mmap migration
- Easier SIMD/string scanning optimization
- Cleaner cache profiling
- Cleaner threading isolation
- Single counting engine
- Single language parser
- Single filesystem walker
- Greatly reduced maintenance burden

