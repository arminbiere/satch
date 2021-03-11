#ifndef _colors_h_INCLUDED
#define _colors_h_INCLUDED

#ifndef NCOLOR

#include <assert.h>
#include <stdbool.h>
#include <unistd.h>

#define COLORS(FD) \
  assert (FD == 1 || FD == 2); \
  bool colors = isatty (FD); \
  FILE * terminal_file = ((FD == 1) ? stdout : stderr); \
  (void) terminal_file		/* needed if 'terminal_file' is not used */

#define BLUE_CODE	"\033[34m"
#define BOLD_CODE	"\033[1m"
#define MAGENTA_CODE	"\033[35m"
#define NORMAL_CODE	"\033[0m"
#define RED_CODE	"\033[31m"
#define YELLOW_CODE	"\033[33m"

#define BLUE		(colors ? BLUE_CODE : "")
#define BOLD		(colors ? BOLD_CODE : "")
#define MAGENTA		(colors ? MAGENTA_CODE : "")
#define NORMAL		(colors ? NORMAL_CODE : "")
#define RED     	(colors ? RED_CODE : "")
#define YELLOW  	(colors ? YELLOW_CODE : "")

#define COLOR(NAME) \
do { \
  if (colors) \
    fputs (NAME ## _CODE, terminal_file); \
} while (0)

#else

#define COLORS(...) do { } while (0)

#define BLUE ""
#define BOLD ""
#define MAGENTA ""
#define NORMAL ""
#define RED ""
#define YELLOW ""

#define COLOR(NAME) do { } while (0)

#endif

#endif
