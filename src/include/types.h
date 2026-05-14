/*
 * types.h — shared data types and constants
 *
 * Structs and constants used in more than 1 of the following, mini-loc-single,
 * mini-loc-multi or output.h Keeping them here in one place avoids the
 * duplicated guarded definitions that previously appeared in every file.
 */

#ifndef LOC_TYPES_H
#define LOC_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Shared constants
 */
#define MAX_EXT_LEN 32
#define MAX_LANGS 1024

#define MAX_COMMENT_LEN 16
#define MAX_LINE_COMMENTS 8
#define MAX_BLOCK_COMMENTS 8
#define MAX_QUOTES 16

#define MAX_LANG_NAME_LEN 32
#define MAX_EXTENSIONS 32
#define MAX_FILENAMES 8
#define MAX_SHEBANGS 8
#define MAX_KEYWORDS 32

#define PATH_BUF 4096
#define MAX_FILE_SIZE (1024L * 1024L)

#define MAX_COMPLEXITY_CHECKS 96
#define MAX_COMPLEXITY_LEN 64

/* 12 Bytes instead of 24 */
typedef struct {
	uint32_t code;
	uint32_t comment;
	uint32_t blank;
	uint8_t padding[52];
} Counts;

/* Reordered the struct to be by cache and with slightly better alignment */

typedef struct {
	char start[MAX_COMMENT_LEN];
	char end[MAX_COMMENT_LEN];

	uint8_t start_len;
	uint8_t end_len;

	bool nested;
} MultiLineRule;

typedef struct {
	char start[MAX_COMMENT_LEN];
	uint8_t len;
} LineCommentRule;

typedef struct {
	char start[8];
	char end[8];

	uint8_t start_len;
	uint8_t end_len;

	bool escape;
	bool multiline;
} QuoteRule;

typedef struct {
	char token[MAX_COMPLEXITY_LEN];
	uint8_t len;
} ComplexityRule;

typedef struct {
	uint16_t n_extensions;
	uint16_t n_filenames;
	uint16_t n_shebangs;
	uint16_t n_keywords;
	uint16_t n_line_comments;
	uint16_t n_multi_line;
	uint16_t n_quotes;
	uint16_t n_complexity;

	bool data_only;
	bool nestedmultiline;

	char name[MAX_LANG_NAME_LEN];
	char complexitychecks_postfix[MAX_EXT_LEN];
	char complexitychecks_postfix_excludes[MAX_EXT_LEN];

	LineCommentRule line_comments[MAX_LINE_COMMENTS];
	char filenames[MAX_FILENAMES][MAX_LANG_NAME_LEN];
	char shebangs[MAX_SHEBANGS][MAX_EXT_LEN];
	MultiLineRule multi_line[MAX_BLOCK_COMMENTS];
	QuoteRule quotes[16];
	char extensions[MAX_EXTENSIONS][MAX_EXT_LEN];
	char keywords[MAX_KEYWORDS][MAX_EXT_LEN];
	ComplexityRule complexity[MAX_COMPLEXITY_CHECKS];

} Language;

typedef enum {
	SCAN_NORMAL = 0,
	SCAN_LINE_COMMENT,
	SCAN_BLOCK_COMMENT,
	SCAN_STRING,
} ScanState;

typedef struct {
	ScanState state;

	int block_index;
	int quote_index;

	uint16_t block_depth;

	bool line_has_code;
	bool line_has_comment;
} Scanner;

/* ext becomes const because it wont change retroactively */
/* Counts is now down to 12 bytes from 24, this changes the setup such that
 * lang_idx can be after counts and fills in the memory location causing us to
 * not need an extra 8 bytes of offset */
typedef struct {
	char* path;
	const char* ext;
	Counts counts;
	int32_t lang_idx;
} FileResult;

/* Doesnt have to be int, make it defined, might make it unsigned later and
 * ensure we never use -1, then we can go down to uint16_t */
typedef struct {
	char ext[MAX_EXT_LEN];
	int32_t lang_idx;
} ExtEntry;

typedef struct {
	const char* path;
	const char* ext;
} LangLookupParams;

typedef struct {
	int num_files;
	int num_langs;
	int max_sums;
} LocSumParams;

typedef enum {
	LOC_FMT_TERMINAL = 0, /* coloured ANSI table — the default */
	LOC_FMT_JSON,
	LOC_FMT_HTML,
	LOC_FMT_SQL,
} LocOutputFormat;

#endif /* LOC_TYPES_H */
