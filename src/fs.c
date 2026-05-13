#include "include/fs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "include/types.h"

void walk_dir(const char* path, bool recurse, FileCallback cb, void* user)
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

void process_path(const char* path, bool recurse, FileCallback cb, void* user)
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
			 "mini-loc: '%s' is a directory (use -r to recurse)\n", path);
		}
		return;
	}
	if (!S_ISREG(st.st_mode)) {
		return;
	}
	cb(path, user);
}
