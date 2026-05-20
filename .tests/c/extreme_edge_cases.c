// Extreme Edge Case File

/* Block comment start, 
   then a "string inside comment" 
   and back to code */
int code = 1;

char* s = "This string has an \"escaped quote\" and a \\ backslash";
char* s2 = "This string spans 
lines, if we supported it";

// Code with odd spacing
   int    spaced   =    2   ;

/* 
 * Very
 * long 
 * comment
 */
 
#define ANOTHER_MACRO /* comment */ 1

int last_line = 3;
// End of file without a newline
int EOF_line = 4;
file_path: tests/c/extreme_edge_cases.c
