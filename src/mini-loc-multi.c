#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "include/cli.h"
#include "include/count.h"
#include "include/fs.h"
#include "include/languages.h"
#include "include/languages_data.h"
#include "include/output.h"
#include "include/threading.h"
#include "include/types.h"

static LocConfig g_cfg;

static void queue_push_cb(const char* path, void* user)
{
	WorkQueue* q = (WorkQueue*) user;
	wq_push(q, strdup(path));
}

static void* worker(void* arg)
{
	ThreadState* ts = (ThreadState*) arg;
	while (true) {
		char* path = wq_pop(ts->queue);
		if (!path) {
			break;
		}
		const char* ext = strrchr(path, '.');
		if (is_ignored_extension(ext)) {
			free(path);
			continue;
		}
		int li = find_language((LangLookupParams) {path, ext});
		if (li == -1) {
			li = find_language((LangLookupParams) {path, NULL});
		}
		if (li == -1 && !g_cfg.list_unknown) {
			free(path);
			continue;
		}
		if (ts->n_files >= ts->capacity) {
			size_t new_capacity =
			 ts->capacity == 0 ? 1024 : (size_t) ts->capacity * 2;
			FileResult* new_files =
			 realloc(ts->files, sizeof(FileResult) * new_capacity);
			if (!new_files) {
				free(ts->files);
				return NULL;
			}
			ts->files = new_files;
			ts->capacity = (int) new_capacity;
		}
		FileResult* fr = &ts->files[ts->n_files++];
		fr->lang_idx = li;
		fr->counts = count_file(path, li);
		fr->path = g_cfg.show_files ? path : NULL;
		fr->ext = (g_cfg.show_files && ext) ? strdup(ext) : NULL;
		if (!g_cfg.show_files) {
			free(path);
		}
	}
	return NULL;
}

__attribute__((cold)) int main(int argc, char** argv)
{
	loc_config_init(&g_cfg);
	parse_cli(&g_cfg, argc, argv);

	load_languages();

	if (g_cfg.lang_load_path) {
		load_languages_from_file(g_cfg.lang_load_path, false);
	}
	if (g_cfg.lang_append_path) {
		load_languages_from_file(g_cfg.lang_append_path, true);
	}

	build_lookup_table();
	WorkQueue queue;
	wq_init(&queue, QUEUE_INIT_CAP);
	long nproc = sysconf(_SC_NPROCESSORS_ONLN);
	if (nproc < 1) {
		nproc = 1;
	}
	if (nproc > MAX_THREADS) {
		nproc = MAX_THREADS;
	}
	int n_threads = (int) nproc;
	pthread_t threads[MAX_THREADS];
	ThreadState states[MAX_THREADS];
	for (int i = 0; i < n_threads; i++) {
		states[i].queue = &queue;
		states[i].files = malloc(sizeof(FileResult) * LOCAL_INIT_CAP);
		states[i].n_files = 0;
		states[i].capacity = LOCAL_INIT_CAP;
		pthread_create(&threads[i], NULL, worker, &states[i]);
	}
	bool any_path = false;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--load") == 0 ||
			 strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--append") == 0 ||
			 strcmp(argv[i], "--filter") == 0 || strcmp(argv[i], "-o") == 0 ||
			 strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-s") == 0 ||
			 strcmp(argv[i], "--sort") == 0) {
				i++;
			}
			continue;
		}
		process_path(argv[i], g_cfg.recurse, queue_push_cb, &queue);
		any_path = true;
	}
	if (!any_path) {
		process_path(".", g_cfg.recurse, queue_push_cb, &queue);
	}
	wq_finish(&queue);
	int total_files = 0;
	for (int i = 0; i < n_threads; i++) {
		pthread_join(threads[i], NULL);
		total_files += states[i].n_files;
	}
	FileResult* all_files = malloc(sizeof(FileResult) * (size_t) total_files);
	int offset = 0;
	for (int i = 0; i < n_threads; i++) {
		memcpy(&all_files[offset], states[i].files,
		 sizeof(FileResult) * (size_t) states[i].n_files);
		offset += states[i].n_files;
		free(states[i].files);
	}
	loc_print_report(g_cfg.output_fmt, all_files, total_files, g_langs,
	 g_n_langs, g_cfg.show_files, g_cfg.verbose, g_cfg.sort_order);
	if (all_files) {
		for (int i = 0; i < total_files; i++) {
			free(all_files[i].path);
		}
		free(all_files);
	}
	wq_destroy(&queue);
	return 0;
}
