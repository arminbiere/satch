/*------------------------------------------------------------------------*/

// This file 'main.c' provides a DIMACS parser and a pretty printer of
// witnesses (satisfying assignments / models) for the stand-alone version
// of the solver binary 'satch'.  For the source code of the solver itself
// see the library code in 'satch.c' with the API provided in 'satch.h'.

// As we use the common 'indent' program (with default style) to format the
// code, the following comment line is necessary to force 'indent' not to
// make a mess out of our nicely formatted 'usage' message.  After the
// definition there is another comment switching formatting on again.

// *INDENT-OFF*

static const char *usage =
"usage: satch [ <option> ... ] [ <dimacs> [ <proof> ] ]\n"
"\n"
"where '<option>' is one of the following\n"
"\n"
"  -h                   print this option summary\n"
"  --version            print solver version and exit\n"
"  --id | --identifier  print GIT hash as identifier\n"
"\n"
"  -a | --ascii         use ASCII format to write proof to file\n"
"  -b | --binary        use binary format to write proof to file\n"
"  -f | --force         overwrite proof files and relax parsing\n"
"  -n | --no-witness    disable printing of satisfying assignment\n"
"\n"
#ifdef LOGGING
"  -l | --log           enable logging messages\n"
#endif
"  -q | --quiet         disable verbose messages\n"
"  -v | --verbose       increment verbose level\n"
"\n"
"or one of these long options setting limits\n"
"\n"
"  --conflicts=<limit>\n"
"\n"
#ifdef _POSIX_C_SOURCE
"and '<dimacs>' is an optionally compressed CNF in DIMACS format by\n"
"default read from '<stdin>'.  For decompression the solver relies on\n"
"external tools 'gzip', 'bunzip2' and 'xz' determined by the path suffix.\n"
#else
"where '<dimacs>' is a CNF in DIMACS format.\n"
#endif
"\n"
"Finally '<proof>' is the path to a file to which if specified a proof\n"
"is written in the DRUP format.  Both '<dimacs>' and '<proof>' can also\n"
"be '-' in which case the input is read from '<stdin>' and the proof is\n"
"written to '<stdout>'. Proofs written to '<stdout>' use the ASCII format\n"
"(unless '--binary' is specified) while proofs written to a file use the\n"
"more compact binary format used in the SAT competition (unless '--ascii'\n"
"is specified).\n"
;

// *INDENT-ON*

/*------------------------------------------------------------------------*/

#include "colors.h"
#include "satch.h"
#include "stack.h"
#include "queue.h"

/*------------------------------------------------------------------------*/

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------*/

// System specific includes for 'stat' and 'access' in 'file_readable'.

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/*------------------------------------------------------------------------*/

// We simply use static global data structures here in 'main.c' which
// implements the stand-alone solver, because the signal handler which
// prints statistics after catching signals requires access to a global
// 'solver' instance anyhow.  The library itself does not use static global
// data data structures and thus can have multiple instances in the same
// process which will not interfere.

/*------------------------------------------------------------------------*/

// Needed by the DIMACS parser.

static struct
{
  int close;			// Set to 0=no-close, 1=fclose, 2=pclose.
  FILE *file;
  const char *path;
} input, proof;

static long lineno = 1;		// Line number for parse error messages.
static uint64_t bytes;		// Read bytes for verbose message.

/*------------------------------------------------------------------------*/

// Static global solver and parsed number of variables.

struct satch *volatile solver;
static int variables;

/*------------------------------------------------------------------------*/

// Global options (we keep the actual option string used to set them).

// Note that global variables in 'C' are initialized to zero and thus these
// options are all disabled initially.

static const char *ascii;	// Force ASCII format for proof files.
static const char *binary;	// Force binary format writing to stdout.
static const char *force;	// Overwrite proofs and relax parsing.

#ifdef LOGGING
const char *logging;
#endif
static const char *quiet;	// Turn off default 'verbose' mode.
const char *no_witness;		// Do not print satisfying assignment.

static int verbose = 1;		// Verbose level (unless 'quiet' is set).

/*------------------------------------------------------------------------*/

// Store parsed XORs for delayed encoding of XORs (particularly with '-f')
// as well as XOR witness checking if compiled with 'NDEBUG' undefined.

static struct int_stack xors;

/*------------------------------------------------------------------------*/

// Line buffer for pretty-printing witnesses ('v' lines following the SAT
// competition output format fit to at most 78 characters per line).

static char buffer[80];
static size_t size_buffer;

/*------------------------------------------------------------------------*/

// Error and verbose messages.

// These declarations provide nice warnings messages if these functions have
// a format string which does not match the type of one of its arguments.

static void error (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

static void fatal_error (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

static void parse_error (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

static void message (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

#ifdef LOGGING

static bool logging_prefix (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

#endif

// After the declaration for catching mismatching format strings, the
// implementation of the actual error and verbose message functions follow.

static void
error (const char *fmt, ...)
{
  COLORS (2);
  va_list ap;
  fprintf (stderr, "%ssatch: %serror: %s", BOLD, RED, NORMAL);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static void
fatal_error (const char *fmt, ...)
{
  COLORS (2);
  va_list ap;
  fprintf (stderr, "%ssatch: %sfatal error: %s", BOLD, RED, NORMAL);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static void
parse_error (const char *fmt, ...)
{
  COLORS (2);
  va_list ap;
  fprintf (stderr, "%ssatch: %sparse error at line %ld in '%s': %s",
	   BOLD, RED, lineno, input.path, NORMAL);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static void
message (const char *fmt, ...)
{
  if (quiet)
    return;
  va_list ap;
  fputs ("c ", stdout);
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  fputc ('\n', stdout);
  fflush (stdout);
}

#ifdef LOGGING

static bool
logging_prefix (const char *fmt, ...)
{
  if (!logging)
    return false;
  COLORS (1);
  COLOR (MAGENTA);
  fputs ("c MAIN 0 ", stdout);
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  return true;
}

static void
logging_suffix (void)
{
  assert (logging);
  COLORS (1);
  COLOR (NORMAL);
  fputc ('\n', stdout);
  fflush (stdout);
}

// Macros hiding making sure that that no code related to logging is
// compiled into the solver if logging is disable at compile-time.

#define LOG(...) \
do { \
  if (logging_prefix (__VA_ARGS__)) \
    logging_suffix ();  \
} while (0)

#else

// Provide and empty macro if logging is disabled.

#define LOG(...) do { } while (0)

#endif

/*------------------------------------------------------------------------*/

// Print the banner of the solver with interesting information.

static void
banner (void)
{
  if (quiet)
    return;
  satch_section (solver, "banner");
  fputs ("c Satch SAT Solver\n", stdout);
  fputs ("c Copyright (c) 2021 Armin Biere JKU Linz\nc\n", stdout);
  printf ("c Version %s", satch_version ());
  if (satch_identifier ())
    printf (" %s", satch_identifier ());
  fputc ('\n', stdout);
  printf ("c Compiled with '%s'\n", satch_compile ());
}

/*------------------------------------------------------------------------*/

// We also allow parsing XOR clauses if the file has an 'p xnf ...' header.

// These XOR clauses are prefixed by an 'x', i.e., the clause 'x -1 2 0'
// means that the variable '1' is equivalent to variable '2'. We simply
// encode those XOR clauses back to CNF by introducing Tseitin variables.

static void
ternary (int a, int b, int c)
{
  satch_add_ternary_clause (solver, a, b, c);
}

static void
quaternary (int a, int b, int c, int d)
{
  satch_add_quaternary_clause (solver, a, b, c, d);
}

static void
direct_xor_encoding (size_t size, int *l)
{
#ifdef LOGGING
  if (logging_prefix ("direct encoding of size %zu XOR", size))
    {
      for (size_t i = 0; i < size; i++)
	printf (" %d", l[i]);
      logging_suffix ();
    }
#endif
  if (!size)
    satch_add_empty (solver);
  else if (size == 1)
    satch_add_unit (solver, l[0]);
  else if (size == 2)
    satch_add_binary_clause (solver, l[0], l[1]),
      satch_add_binary_clause (solver, -l[0], -l[1]);
  else if (size == 3)
    ternary (-l[0], -l[1], l[2]), ternary (-l[0], l[1], -l[2]),
      ternary (l[0], -l[1], -l[2]), ternary (l[0], l[1], l[2]);
  else
    {
      assert (size == 4);
      quaternary (-l[0], -l[1], -l[2], -l[3]);
      quaternary (-l[0], -l[1], l[2], l[3]);
      quaternary (-l[0], l[1], -l[2], l[3]);
      quaternary (-l[0], l[1], l[2], -l[3]);
      quaternary (l[0], -l[1], -l[2], l[3]);
      quaternary (l[0], -l[1], l[2], -l[3]);
      quaternary (l[0], l[1], -l[2], -l[3]);
      quaternary (l[0], l[1], l[2], l[3]);
    }
}

// This elegant XOR constraint encoding due to Marijn Heule requires the
// least number of variables and clauses.  In essence the XOR is translated
// into a ternary tree and variables are introduced in a layered fashion.

// First we introduce a layer of n/3 variables, where each of them
// represents the parity of three input variables.  Then for each triple of
// those introduced variables we add in the second layer a new variable etc.
// For an XOR over four variables or less we use a direct encoding.

static int
encode_xor (int tseitin, size_t size, int *literals)
{
  struct int_queue q;
  INIT_QUEUE (q);
  for (size_t i = 0; i < size; i++)
    ENQUEUE (q, literals[i]);
  while ((size = SIZE_QUEUE (q)) > 4)
    {
      tseitin++;
      int t[4] = { DEQUEUE (q), DEQUEUE (q), DEQUEUE (q), -tseitin };
      LOG ("new variable %d = %d ^ %d ^ %d", tseitin, t[0], t[1], t[2]);
      direct_xor_encoding (4, t);
      ENQUEUE (q, tseitin);
    }
  direct_xor_encoding (size, q.head);
  RELEASE_QUEUE (q);
  return tseitin;
}

// In forced parsing mode we do not know the number of actual variables
// while parsing an XOR clause and since we need to introduce Tseitin
// variables to encode an XOR we need to wait until we have determined the
// maximum variable index occurring in the file before encoding XORs.

// As a side-effect of postponing the encoding of XOR clauses in forced
// parsing mode the Tseitin variables will be activated in a different
// order, which will change the (initial) order how these variables are
// picked as decisions (in forced mode they will always be picked first).

// As consequence there is a high chance that the SAT solver will behave
// differently after parsing an XNF in forced mode (for CNF in DIMACS this
// issue does not occur since variables are activated in the same way).

static void
encode_xors (int tseitin, size_t start)
{
  int *x = xors.begin + start;
  const int *const end = xors.end;
  for (int *y = x; x != end; x = y + 1)
    {
      for (y = x; assert (y != end), *y; y++)
	;
      tseitin = encode_xor (tseitin, (size_t) (y - x), x);
    }
}

#ifndef NDEBUG

// The XORs are not seen by the library and thus we need to check models
// returned by the library manually here (this is mainly used to catch
// potential issues with incorrect encoding code).

static void
check_xors_satisfied (void)
{
  if (EMPTY_STACK (xors))
    return;

  const int *x = xors.begin;
  const int *const end = xors.end;
  size_t checked = 0;

  for (const int *y = x; x != end; x = y + 1)
    {
      checked++;

      bool satisfied = false;
      int partial = 0;

      int lit;
      for (y = x; assert (y != end), (lit = *y); y++)
	{
	  int tmp = satch_val (solver, lit);
	  if (!tmp)
	    partial = lit;
	  else if (tmp == lit)
	    satisfied = !satisfied;
	  else
	    assert (tmp == -lit);
	}

      if (!partial && satisfied)
	continue;

      COLORS (2);
      fflush (stdout);
      fprintf (stderr, "%slibsatch: %sfatal error: %s", BOLD, RED, NORMAL);
      if (partial)
	fprintf (stderr, "partial assignment of %d in", partial);
      else
	fputs ("unsatisfied", stderr);
      fprintf (stderr, " size %zu XOR:\n", (size_t) (y - x));
      for (const int *z = x; (lit = *z); z++)
	fprintf (stderr, "%d ", lit);
      fputs ("0\n", stderr);
      fflush (stderr);
      abort ();
    }
#ifdef LOGGING
  if (logging)
    LOG ("checked all %zu XORs to be satisfied", checked);
#endif
}

#endif

/*------------------------------------------------------------------------*/

// This parser for DIMACS files is meant to be pretty robust and precise.
// For instance it carefully checks that the number of variables as well as
// literals are valid 32-two bit integers (different from 'INT_MIN'). For
// the number of clauses it uses 'size_t'.  Thus in a 64-bit environment it
// can parse really large CNFs with 2^32 clauses and more.

// The following function reads a character from the global input file,
// squeezes out carriage return characters (after checking that they are
// followed by a newline) and maintains read bytes and lines statistics.

static inline int
next (void)
{
  int res = getc (input.file);
  if (res == '\r')		// Care for DOS / Windows '\r\n'.
    {
      bytes++;
      res = getc (input.file);
      if (res != '\n')
	parse_error ("expected new line after carriage return");
    }
  if (res == '\n')
    lineno++;
  if (res != EOF)
    bytes++;
  return res;
}

// Needed at several places to print statistics.

static inline double
percent (double a, double b)
{
  return b ? 100 * a / b : 0;
}

// This is the actual DIMACS file parser.  It uses the 'next' function to
// read bytes from the global file.  Beside proper error messages in case of
// parse errors it also prints information about parsed clauses etc.

// The file is not opened here, since we want to print the 'banner' in
// 'main' after checking that we can really access and open the file.  But
// it is closed in this function to print the information just discussed at
// the right place where this should happen.

static void
parse (void)
{
  satch_start_profiling_parsing (solver);

  if (!quiet)
    {
      satch_section (solver, "parsing");
      message ("%sparsing '%s'", force ? "force " : "", input.path);
    }

  int ch;
  while ((ch = next ()) == 'c')
    {
      while ((ch = next ()) != '\n')
	if (ch == EOF)
	  parse_error ("unexpected end-of-file in header comment");
    }
  if (ch != 'p')
    parse_error ("expected 'p' or 'c'");
  if (next () != ' ')
    parse_error ("expected space after 'p'");
  char format = next ();
  if (format != 'c' && format != 'x')
    parse_error ("expected 'c' or 'x' after 'p '");
  if (next () != 'n')
    parse_error ("expected 'n' after 'p c'");
  if (next () != 'f')
    parse_error ("expected 'f' after 'p cn'");
  if (next () != ' ')
    parse_error ("expected space after 'p %cnf'", format);
  while ((ch = next ()) == ' ' || ch == '\t')
    ;
  if (!isdigit (ch))
    parse_error ("expected digit after 'p %cnf '", format);
  variables = ch - '0';
  while (isdigit (ch = next ()))
    {
      if (!variables)
	parse_error ("invalid digit after '0' "
		     "while parsing maximum variable");
      if (INT_MAX / 10 < variables)
	parse_error ("maximum variable number way too big");
      variables *= 10;
      const int digit = ch - '0';
      if (INT_MAX - digit < variables)
	parse_error ("maximum variable number too big");
      variables += digit;
    }
  if (ch != ' ')
    parse_error ("expected space after 'p %cnf %d'", format, variables);
  while ((ch = next ()) == ' ' || ch == '\t')
    ;
  if (!isdigit (ch))
    parse_error ("expected digit after 'p %cnf %d '", format, variables);
  size_t specified_clauses = ch - '0';
  while (isdigit (ch = next ()))
    {
      if (!specified_clauses)
	parse_error ("invalid digit after '0' "
		     "while parsing number of clauses");
      const size_t MAX_SIZE_T = ~(size_t) 0;
      if (MAX_SIZE_T / 10 < specified_clauses)
	parse_error ("way too many clauses specified");
      specified_clauses *= 10;
      const int digit = ch - '0';
      if (MAX_SIZE_T - digit < specified_clauses)
	parse_error ("too many clauses specified");
      specified_clauses += digit;
    }
  if (ch == ' ' || ch == '\t')
    {
      while ((ch = next ()) == ' ' || ch == '\t')
	;
    }
  if (ch != '\n')
    parse_error ("expected new line after 'p %cnf %d %zu'", format,
		 variables, specified_clauses);

  message ("parsed 'p %cnf %d %zu' header",
	   format, variables, specified_clauses);
  satch_reserve (solver, variables);

  int parsed_variables = 0;	// Maximum parsed variable index.
  size_t parsed_clauses = 0;
  size_t parsed_xors = 0;

  size_t offset_of_encoded_xors = 0;

  int tseitin = force ? 0 : variables;
  char type = 0;
  int lit = 0;

  for (;;)
    {
      ch = next ();

      // Skip white space.

      if (ch == ' ' || ch == '\t' || ch == '\n')
	continue;

      if (ch == EOF)
	break;

      // Read and skip comments.

      if (ch == 'c')
	{

	COMMENT:		// See below on why we need 'goto' here.

	  while ((ch = next ()) != '\n')
	    if (ch == EOF)
	      parse_error ("unexpected end-of-file in comment");
	  continue;
	}

      // Read XOR type.

      if (ch == 'x')
	{
	  if (lit)
	    parse_error ("'x' after non-zero %d'", lit);
	  if (type)
	    parse_error ("'x' after '%c'", type);
	  if (!force && format != 'x')
	    parse_error ("unexpected 'x' in CNF (use 'p xnf ...' header)");
	  type = 'x';
	  continue;
	}

      // Get sign of next literal and its first digit.

      int sign = 1;

      if (ch == '-')
	{
	  ch = next ();
	  if (!isdigit (ch))
	    parse_error ("expected digit after '-'");
	  if (ch == '0')
	    parse_error ("expected non-zero digit after '-'");
	  sign = -1;
	}
      else if (!isdigit (ch))
	parse_error ("expected number");

      // In forced parsing mode we ignore specified clauses.

      if (!force)
	{
	  assert (parsed_clauses <= specified_clauses);
	  if (parsed_clauses == specified_clauses)
	    parse_error ("more clauses than specified");
	}

      // Read the variable index and make sure not to overflow.

      int idx = ch - '0';
      while (isdigit (ch = next ()))
	{
	  if (!idx)
	    parse_error ("invalid digit after '0' in number");
	  if (INT_MAX / 10 < idx)
	    parse_error ("number way too large");
	  idx *= 10;
	  const int digit = ch - '0';
	  if (INT_MAX - digit < idx)
	    parse_error ("number too large");
	  idx += digit;
	}

      // Now we have the variable with its sign as parsed literal.

      lit = sign * idx;

      // Be careful to check the character after the last digit.

      if (ch != ' ' && ch != '\t' && ch != '\n' && ch != 'c')
	parse_error ("unexpected character after '%d'", lit);

      assert (lit != INT_MIN);
      if (!force && idx > variables)
	parse_error ("literal '%d' exceeds maximum variable index '%d'",
		     lit, variables);

      if (idx > parsed_variables)
	parsed_variables = idx;

      if (!lit)
	parsed_clauses++;

      if (!type)
	{
	  // The IPASIR semantics of 'satch_add' in essence just gets the
	  // numbers in the DIMACS file after the header and 'adds' them
	  // including the zeroes terminating each clause.  Thus we do not
	  // have to use another function for adding a clause explicitly.

	  satch_add (solver, lit);
	}
      else if (lit)
	{
	  assert (type == 'x');
	  PUSH (xors, lit);
	}
      else
	{
	  assert (type == 'x');
	  type = 0;

	  // As described above (before 'encode_xors'), in forced parsing
	  // mode we need to wait until we know the maximum variable in the
	  // file before we can start encoding XORs.  In precise parsing
	  // mode we can simply encode the XOR directly (which also is
	  // beneficial to activate and place Tseitin variables close to the
	  // other variables seen so far and thus in this XOR clause).

	  const size_t new_offset = SIZE_STACK (xors);
	  const size_t size = new_offset - offset_of_encoded_xors;
	  int *x = xors.begin + offset_of_encoded_xors;

	  if (force)
	    {
#ifdef LOGGING
	      if (logging_prefix ("parsed size %zu XOR", size))
		{
		  for (const int *p = x; x != xors.end; x++)
		    printf (" %d", *p);
		  logging_suffix ();
		}
#endif
	      PUSH (xors, 0);
	    }
	  else
	    {
	      tseitin = encode_xor (tseitin, size, x);
#ifndef NDEBUG
	      PUSH (xors, 0);
	      offset_of_encoded_xors = new_offset + 1;
	      assert (offset_of_encoded_xors == SIZE_STACK (xors));
#else
	      CLEAR_STACK (xors);
	      assert (!offset_of_encoded_xors);
#endif
	    }

	  parsed_xors++;
	}

      // The following 'goto' is necessary to avoid reading another
      // character which would result in a spurious parse error for a comment
      // immediately starting after a literal, e.g., as in '1comment'.

      if (ch == 'c')
	goto COMMENT;
    }

  if (lit)
    parse_error ("terminating zero after literal '%d' missing", lit);

  if (type)
    assert (format == 'x'), parse_error ("literals missing after 'x'");

  if (!force && parsed_clauses < specified_clauses)
    {
      if (parsed_clauses + 1 == specified_clauses)
	parse_error ("single clause missing");
      else
	parse_error ("%zu clauses missing",
		     specified_clauses - parsed_clauses);
    }

  // Handle delayed XOR encoding in forced parsing mode.

  if (!EMPTY_STACK (xors))
    encode_xors (parsed_variables, offset_of_encoded_xors);

  const double seconds = satch_stop_profiling_parsing (solver);

  if (parsed_clauses == 1)
    message ("parsed exactly one clause in %.2f seconds", seconds);
  else
    message ("parsed %zu clauses in %.2f seconds", parsed_clauses, seconds);

  if (parsed_xors == 1)
    message ("including exactly one XOR clause %.0f%%",
	     percent (1, parsed_clauses));
  else if (parsed_xors > 1)
    message ("including %zu XOR clauses %.0f%%",
	     parsed_xors, percent (parsed_xors, parsed_clauses));
  else if (format == 'x')
    assert (!parsed_xors), message ("without any XOR clauses");

  if (parsed_variables == 0)
    message ("input file does not contain any variable");
  else
    message ("found maximum variable index %d", parsed_variables);

  if (force && variables < parsed_variables)
    variables = parsed_variables;

  if (input.close == 1)		// Opened with 'fopen'.
    fclose (input.file);

#ifdef _POSIX_C_SOURCE
  if (input.close == 2)		// Opened with 'popen'.
    pclose (input.file);
#endif

  message ("closed '%s'", input.path);
  message ("after reading %" PRIu64 " bytes (%.0f MB)",
	   bytes, bytes / (double) (1 << 20));

#ifdef NDEBUG
  RELEASE_STACK (xors);
#endif
}

/*------------------------------------------------------------------------*/

// These two functions support pretty printing of satisfying assignments.
// According to the SAT competition output format these witnesses consist of
// 'v ...' lines containing the literals which are true followed by '0'.  We
// want to restrict these lines to 78 characters (including the 'v ' prefix)
// and use an output line buffer (of 80 characters in size) for that.

static void
flush_printed_values (void)
{
  if (!size_buffer)
    return;
  assert (size_buffer + 1 < sizeof buffer);
  buffer[size_buffer++] = 0;
  fputc ('v', stdout);
  fputs (buffer, stdout);
  fputc ('\n', stdout);
  size_buffer = 0;
}

static inline void
print_value (int lit)
{
  char tmp[32];
  sprintf (tmp, " %d", lit);
  const size_t size_tmp = strlen (tmp);
  if (size_buffer + size_tmp > 77)	// Care for 'v'.
    flush_printed_values ();
  memcpy (buffer + size_buffer, tmp, size_tmp);
  size_buffer += size_tmp;
}

/*------------------------------------------------------------------------*/

// For compressed files just opening a pipe will not return a zero file
// pointer if the file does not exist.  Instead this would produce a strange
// error message and thus we always check for being able to access the file
// explicitly (even though this is only needed for compressed files).  We
// use two low-level functions 'stat' and 'access' for this check which
// makes this code slightly more operating system dependent.

bool
file_readable (const char *path)
{
  if (!path)
    return false;
  struct stat buf;
  if (stat (path, &buf))
    return false;
  if (access (path, R_OK))
    return false;
  return true;
}

/*------------------------------------------------------------------------*/

// Without POSIX support (usually enabled through './configure --pedantic'
// which in turn enforces '-Werror -std=c99 --pedantic' as compiler options)
// we do not support compressed input files since 'popen' is missing.
// Otherwise we rely on external decompression tools and a pipe.

#ifdef _POSIX_C_SOURCE

static bool
has_suffix (const char *str, const char *suffix)
{
  const size_t l = strlen (str), k = strlen (suffix);
  return l >= k && !strcmp (str + l - k, suffix);
}

// Open a pipe to a command given as a 'printf' style format string which is
// expected to contain exactly one '%s' which is replaced by the path.

static void
open_pipe (const char *fmt)
{
  char *cmd = malloc (strlen (fmt) + strlen (input.path));
  if (!cmd)
    error ("out-of-memory allocating command buffer");
  sprintf (cmd, fmt, input.path);
  input.file = popen (cmd, "r");
  input.close = 2;		// Make sure to use 'pclose' on closing.
  free (cmd);
}

#endif

/*------------------------------------------------------------------------*/

// Signal handlers to print statistics in case of interrupts etc.

static volatile int caught_signal;

// We are using 'SIG...' both as integer constant as well as string and use
// the trick to collect all possible signal names in a 'SIGNALS' macros.
// That can be instantiated with different interpretations of 'SIGNAL'
// avoiding repetition of signal code which only differs in the signal name.

#define SIGNALS \
SIGNAL(SIGABRT) \
SIGNAL(SIGBUS) \
SIGNAL(SIGINT) \
SIGNAL(SIGSEGV) \
SIGNAL(SIGTERM)

// *INDENT-OFF*

// Saved previous signal handlers.

#define SIGNAL(SIG) \
static void (*saved_ ## SIG ## _handler)(int);
SIGNALS
#undef SIGNAL

static void
reset_signal_handler (void)
{
#define SIGNAL(SIG) \
  signal (SIG, saved_ ## SIG ## _handler);
  SIGNALS
#undef SIGNAL
}

static void
catch_signal (int sig)
{
  if (caught_signal)
    return;
  caught_signal = sig;
  const char *name = "SIGNUNKNOWN";
#define SIGNAL(SIG) \
  if (sig == SIG) name = #SIG;
  SIGNALS
#undef SIGNAL
  if (!quiet)
    {
      COLORS (1);
      fputs ("c\nc ", stdout);
      COLOR (RED);
      COLOR (BOLD);
      printf ("caught signal %d (%s)", sig, name);
      COLOR (NORMAL);
      fputc ('\n', stdout);
      fflush (stdout);
      satch_statistics (solver);
      fputs ("c\nc ", stdout);
      COLOR (RED);
      COLOR (BOLD);
      printf ("raising signal %d (%s)", sig, name);
      COLOR (NORMAL);
      fputc ('\n', stdout);
      fflush (stdout);
    }
  reset_signal_handler ();
  raise (sig);
}

static void
init_signal_handler (void)
{
#define SIGNAL(SIG) \
  saved_ ## SIG ##_handler = signal (SIG, catch_signal);
  SIGNALS
#undef SIGNAL
}

/*------------------------------------------------------------------------*/

// We allow to use command line options only once.  The first usage 'a' is
// stored at the given pointer 'p' and is checked not be set already.

static void
set_option (const char **p, const char *a)
{
  if (!*p)
    *p = a;
  else if (!strcmp (*p, a))
    error ("multiple '%s'", a);
  else
    error ("redundant '%s' and '%s'", *p, a);
}

// The next function parses a long option with a 32-bit integer argument
// precisely and checks for under- and overflow.

// The first argument 'arg' is the string giving as long option on the
// command line.  The 'name' denotes the name of the option (without leading
// '--' and trailing '=...').  The third argument is used to store the 'arg'
// string (with 'set_option' above).  The actual value is returned through
// the last pointer.

// Checking under- and overflow requires two separate loops below (since
// 'INT_MAX < -INT_MIN' and the second loop would fail for 'INT_MIN').

static bool
parse_int_option (const char *arg,
		  const char *name, const char **option_ptr, int *value_ptr)
{
  if (*arg++ != '-')
    return false;
  if (*arg++ != '-')
    return false;
  int ch;
  while ((ch = *arg++) && ch == *name)
    name++;
  if (*name)
    return false;
  if (ch != '=')
    return false;
  int value;
  if ((ch = *arg++) == '-')
    {
      ch = *arg++;
      if (!isdigit (ch))
	return false;
      value = '0' - ch;
      while (isdigit (ch = *arg++))
	{
	  if (INT_MIN / 10 > value)
	    return false;
	  value *= 10;
	  const int digit = ch - '0';
	  if (INT_MIN + digit > value)
	    return false;
	  value -= digit;
	}
    }
  else
    {
      if (!isdigit (ch))
	return false;
      value = ch - '0';
      while (isdigit (ch = *arg++))
	{
	  if (INT_MAX / 10 < value)
	    return false;
	  value *= 10;
	  const int digit = ch - '0';
	  if (INT_MAX - digit < value)
	    return false;
	  value += digit;
	}
    }
  if (ch)
    return false;
  set_option (option_ptr, arg);
  *value_ptr = value;
  return true;
}

/*------------------------------------------------------------------------*/

int
main (int argc, char **argv)
{
  const char *conflict_option = 0;
  int conflict_limit = -1;

  for (int i = 1; i < argc; i++)
    {
      const char *arg = argv[i];

      if (!strcmp (arg, "-h"))
	fputs (usage, stdout), exit (0);
      if (!strcmp (arg, "--version"))
	printf ("%s\n", satch_version ()), exit (0);
      if (!strcmp (arg, "--id") || !strcmp (arg, "--identifier"))
	printf ("%s\n", satch_identifier ()), exit (0);

      else if (!strcmp (arg, "-a") || !strcmp (arg, "--ascii"))
	set_option (&ascii, arg);
      else if (!strcmp (arg, "-b") || !strcmp (arg, "--binary"))
	set_option (&binary, arg);
      else if (!strcmp (arg, "-f") || !strcmp (arg, "--force"))
	set_option (&force, arg);
      else if (!strcmp (arg, "-n") || !strcmp (arg, "--no-witness"))
	set_option (&no_witness, arg);

      else if (!strcmp (arg, "-l") || !strcmp (arg, "--log"))
#ifdef LOGGING
	set_option (&logging, arg);
#else
	error ("solver configured without logging support");
#endif
      else if (!strcmp (arg, "-q") || !strcmp (arg, "--quiet"))
	set_option (&quiet, arg);
      else if (!strcmp (arg, "-v") || !strcmp (arg, "--verbose"))
	verbose += (verbose < INT_MAX);
      else if (parse_int_option (arg, "conflicts",
				 &conflict_option, &conflict_limit))
	{
	  if (conflict_limit < 0)
	    error ("negative conflict limit '%d' in '%s'",
		   conflict_limit, arg);
	}
      else if (arg[0] == '-' && arg[1])
	error ("invalid command option '%s' (try '-h')", arg);
      else if (proof.path)
	error ("too many files '%s', '%s' and '%s' (try '-h')",
	       input.path, proof.path, arg);
      else if (input.path)
	proof.path = arg;
      else
	input.path = arg;
    }

#ifdef LOGGING
  if (quiet && logging)
    error ("can not combine '%s' and '%s'", quiet, logging);
#endif
  if (quiet && verbose > 1)
    error ("can use '%s' and increase verbosity", quiet);
  solver = satch_init ();
  if (!solver)
    error ("failed to initialize solver");
  if (!quiet)
    satch_set_verbose_level (solver, verbose);
#ifdef LOGGING
  if (logging)
    satch_enable_logging_messages (solver);
#endif

  if (ascii && binary)
    error ("both '%s' and '%s' specified", ascii, binary);
  if (ascii && !proof.path)
    error ("invalid '%s' without proof file", ascii);
  if (binary && !proof.path)
    error ("invalid '%s' without proof file", binary);
  if (ascii && proof.path && !strcmp (proof.path, "-"))
    error ("invalid '%s' for proofs written to '<stdout>'", ascii);
  if (binary && proof.path && strcmp (proof.path, "-"))
    error ("invalid '%s' for proof written to a file", binary);
  if (binary && proof.path && !strcmp (proof.path, "-") && isatty (1))
    error ("not writing binary proof to terminal ('%s' and '-')", binary);

  if (!force &&
      proof.path &&
      strcmp (proof.path, "-") &&
      strcmp (proof.path, "/dev/null") && file_readable (proof.path))
    error ("will not overwrite '%s' without '-f' (try '-h')", proof.path);

  if (!input.path || !strcmp (input.path, "-"))
    input.path = "<stdin>", input.file = stdin;
#ifdef _POSIX_C_SOURCE
  else if (!file_readable (input.path))
    error ("can not access '%s'", input.path);
  else if (has_suffix (input.path, ".gz"))
    open_pipe ("gzip -c -d %s");
  else if (has_suffix (input.path, ".bz2"))
    open_pipe ("bzip2 -c -d %s");
  else if (has_suffix (input.path, ".xz"))
    open_pipe ("xz -c -d %s");
#endif
  else
    input.file = fopen (input.path, "r"), input.close = 1;
  if (!input.file)
    error ("can not read DIMACS file '%s'", input.path);

  init_signal_handler ();
  banner ();

  if (proof.path)
    {
      if (!strcmp (proof.path, "-"))
	{
	  proof.path = "-", proof.file = stdout;
	  if (!binary)
	    ascii = "use-ASCII-format-by-default-when-writing-to-stdout";
	}
      else if ((proof.file = fopen (proof.path, "w")))
	proof.close = 1;
      else
	error ("can not write DRUP file '%s'", proof.path);

      if (ascii)
	satch_ascii_proof (solver);
      satch_trace_proof (solver, proof.file);
    }

  parse ();

  if (conflict_option && !quiet)
    {
      satch_section (solver, "limits");
      message ("conflict limit set to %d conflicts", conflict_limit);
    }

  int res = satch_solve (solver, conflict_limit);

  if (proof.file)
    {
      if (proof.close == 1)
	fclose (proof.file);
#ifdef _POSIX_C_SOURCE
      if (proof.close == 2)
	pclose (proof.file);
#endif
    }

  if (!quiet)
    satch_section (solver, "result");
  if (res == SATISFIABLE)
    {
#ifndef NDEBUG
      check_xors_satisfied ();
#endif
      printf ("s SATISFIABLE\n");
      if (!no_witness)
	{
	  for (int i = 1; i <= variables; i++)
	    print_value (satch_val (solver, i));
	  print_value (0);
	  flush_printed_values ();
	}
      fflush (stdout);
    }
  else if (res == UNSATISFIABLE)
    {
      printf ("s UNSATISFIABLE\n");
      fflush (stdout);
    }
  else
    message ("no result");

  if (!quiet)
    {
      satch_statistics (solver);
      fflush (stdout);
    }

  reset_signal_handler ();

  if (!quiet)
    satch_section (solver, "shutting down");
  satch_release (solver);
#ifndef NDEBUG
  RELEASE_STACK (xors);
#endif
  message ("exit %d", res);

  return res;
}
