/*
 * types.h — shared data types and constants
 *
 * Structs and constants used in more than 1 of the following, mini-loc-single,
 * mini-loc-multi or output.h Keeping them here in one place avoids the
 * duplicated guarded definitions that previously appeared in every file.
 */

#ifndef LOC_TYPES_H
#define LOC_TYPES_H

#include <cstdint>
#include <stdbool.h>
#include <stddef.h>

/*
 * Shared constants
 */
#define MAX_EXT_LEN 32
#define MAX_LANGS 512
#define MAX_COMMENT_LEN 16
#define MAX_LINE_COMMENTS 8
#define MAX_BLOCK_COMMENTS 8
#define MAX_LANG_NAME_LEN 32
#define MAX_EXTENSIONS 32
#define PATH_BUF 4096
#define MAX_FILE_SIZE (1024L * 1024L)

/* 12 Bytes instead of 24 */
typedef struct {
	uint32_t code;
	uint32_t comment;
	uint32_t blank;
} Counts;

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

/* ext becomes const because it wont change retroactively */
/* Counts is now down to 12 bytes from 24, this changes the setup such that
 * lang_idx can be after counts and fills in the memory location causing us to
 * not need an extra 8 bytes of offset */
typedef struct {
	char* path;
	const char* ext;
	Counts counts;
	int lang_idx;
} FileResult;

typedef struct {
	char ext[MAX_EXT_LEN];
	int lang_idx;
} ExtEntry;

typedef struct {
	const char* path;
	const char* ext;
} LangLookupParams;

#endif /* LOC_TYPES_H */
