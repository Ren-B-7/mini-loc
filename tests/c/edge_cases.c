/* This file contains edge cases for line counting */

/* Multi-line comment 
   that should be counted as comment lines.
*/

int x = 10; // Inline comment with code before it
int y = 20;

/* Nested /* block */ comment */

#define MACRO_TEST 1 /* Comment on macro line */

// Line comment
#ifdef DEBUG
   int debug_var = 1;
#endif

"String with // inside it" 
"String with /* inside it */"

/*
   Comment with 
   blank line in middle 
*/

int z = 30;
