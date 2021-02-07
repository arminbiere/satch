/*------------------------------------------------------------------------*/
//   Copyright (c) 2021, Armin Biere, Johannes Kepler University Linz     //
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
#ifndef NDEBUG
"  -l | --log           enable logging messages\n"
#endif
"  -q | --quiet         disable verbose messages\n"
"  -v | --verbose       increment verbose level\n"
"\n"
#ifdef _POSIX_C_SOURCE
"where '<dimacs>' is an optionally compressed CNF in DIMACS format by\n"
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

#include "satch.h"

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
  int close;			// 0=no-close, 1=fclose, 2=pclose
  FILE *file;
  const char *path;
} input, proof;

static long lineno = 1;		// line number for parse error messages
static uint64_t bytes;		// read bytes for verbose message

/*------------------------------------------------------------------------*/

// Static global solver and number of variables.

struct satch *volatile solver;
static int variables;

/*------------------------------------------------------------------------*/

// Global options (we keep the actual option string used to set them).
// Note that global variables in 'C' are initialized to zero and thus these
// options are all disabled initially.

static const char *ascii;	// Force ASCII format for proof files.
static const char *binary;	// Force binary format writing to stdout.
static const char *force;	// Overwrite proofs and relax parsing.

#ifndef NDEBUG
const char *logging;
#endif
static const char *quiet;	// Turn off default 'verbose' mode.
const char *no_witness;		//

static int verbose = 1;		// Verbose level (unless 'quiet' is set).

/*------------------------------------------------------------------------*/

// Line buffer for pretty-printing witnesses ('v' lines following the SAT
// competition output format formatted to at most 78 characters per line).

static char buffer[80];
static size_t size_buffer;

/*------------------------------------------------------------------------*/

// Error and verbose messages.

// These declarations provide nice warnings messages if these functions have
// a format string which does not match the type of one of its arguments.

static void error (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

static void parse_error (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

static void message (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

static void
error (const char *fmt, ...)
{
  va_list ap;
  fputs ("satch: error: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static void
parse_error (const char *fmt, ...)
{
  va_list ap;
  fprintf (stderr, "satch: parse error at line %ld in '%s': ",
	   lineno, input.path);
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

// This parser for DIMACS files is meant to be pretty robust and precise.
// For instance it carefully checks that the number of variables as well as
// literals are valid 32-two bit integers (different from 'INT_MIN'). For
// the number of clauses it uses 'size_t'.  Thus in a 64-bit environment it
// can parse really large CNFs with 2^32 clauses and more.

// The following function reads a character from the global file variable,
// squeezes out carriage return characters (before checking that they are
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
  if (next () != 'c')
    parse_error ("expected 'c' after 'p '");
  if (next () != 'n')
    parse_error ("expected 'n' after 'p c'");
  if (next () != 'f')
    parse_error ("expected 'f' after 'p cn'");
  if (next () != ' ')
    parse_error ("expected space after 'p cnf'");
  while ((ch = next ()) == ' ' || ch == '\t')
    ;
  if (!isdigit (ch))
    parse_error ("expected digit after 'p cnf '");
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
    parse_error ("expected space after 'p cnf %d'", variables);
  while ((ch = next ()) == ' ' || ch == '\t')
    ;
  if (!isdigit (ch))
    parse_error ("expected digit after 'p cnf %d '", variables);
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
    parse_error ("expected new line after 'p cnf %d %zu'", variables,
		 specified_clauses);

  message ("parsed 'p cnf %d %zu' header", variables, specified_clauses);
  satch_reserve (solver, variables);

  size_t parsed_clauses = 0;
  int parsed_variables = 0;

  int lit = 0;

  for (;;)
    {
      ch = next ();
      if (ch == ' ' || ch == '\t' || ch == '\n')
	continue;
      if (ch == EOF)
	break;
      if (ch == 'c')
	{
	COMMENT:
	  while ((ch = next ()) != '\n')
	    if (ch == EOF)
	      parse_error ("unexpected end-of-file in comment");
	  continue;
	}

      int sign = 1;

      if (ch == '-')
	{
	  ch = next ();
	  if (!isdigit (ch))
	    parse_error ("expected digit after '-'");
	  sign = -1;
	}
      else if (!isdigit (ch))
	parse_error ("expected number");

      if (!force)
	{
	  assert (parsed_clauses <= specified_clauses);
	  if (parsed_clauses == specified_clauses)
	    parse_error ("more clauses than specified");
	}

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

      lit = sign * idx;

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

      // The IPASIR semantics of 'satch_add' in essence just gets the
      // numbers in the DIMACS file after the header and 'adds' them
      // including the zeroes terminating each clause.  Thus we do not have
      // to use another function for adding a clause explicitly.
      //
      satch_add (solver, lit);

      // The following 'goto' is necessary to avoid reading another
      // character which would result in a spurious parse error for a comment
      // immediately starting after a literal, e.g., as in '1comment'.
      //
      if (ch == 'c')
	goto COMMENT;
    }

  if (lit)
    parse_error ("terminating zero after literal '%d' missing", lit);

  if (!force && parsed_clauses < specified_clauses)
    {
      if (parsed_clauses + 1 == specified_clauses)
	parse_error ("single clause missing");
      else
	parse_error ("%zu clauses missing",
		     specified_clauses - parsed_clauses);
    }

  const double seconds = satch_stop_profiling_parsing (solver);

  if (parsed_clauses == 1)
    message ("parsed exactly one clause in %.2f seconds", seconds);
  else
    message ("parsed %zu clauses in %.2f seconds", parsed_clauses, seconds);

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
}

/*------------------------------------------------------------------------*/

// These two functions support pretty printer of satisfying assignments.
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
// explicitly (even though this is only need for compressed files).  We use
// two low-level functions 'stat' and 'access' for this check which
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

// *INDENT-ON*

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
      printf ("c\nc caught signal %d ('%s')\n", sig, name);
      fflush (stdout);
      satch_statistics (solver);
      printf ("c\nc raising signal %d ('%s')\nc\n", sig, name);
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

int
main (int argc, char **argv)
{
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
#ifdef NDEBUG
	error ("solver configured without logging support");
#else
	set_option (&logging, arg);
#endif
      else if (!strcmp (arg, "-q") || !strcmp (arg, "--quiet"))
	set_option (&quiet, arg);
      else if (!strcmp (arg, "-v") || !strcmp (arg, "--verbose"))
	verbose += (verbose < INT_MAX);

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

#ifndef NDEBUG
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
#ifndef NDEBUG
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
  int res = satch_solve (solver);

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
  message ("exit %d", res);

  return res;
}
