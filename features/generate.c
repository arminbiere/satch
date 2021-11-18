/*------------------------------------------------------------------------*/

// *INDENT-OFF*

static const char * usage =
"usage: generate [-h|-p|-l|-v|] [ all | [ <file> ... ] ]\n"
"\n"
"  -h  print this command line option summary\n"
"  -p  pedantically treat unsorted features and pairs as error\n"
"  -l  list features files that can be generated\n"
"  -v  increase verbose level\n"
"\n"
"Without any '<file>' all files are generated (also for 'all').\n";

// *INDENT-ON*

// At one point it became more and more tedious to maintain all the
// feature dependencies in different files ('configure', 'mkconfig.sh',
// 'features.h' and 'gencombi.c') and instead of manually ensuring the
// consistency we generate them from one global set of files.
//
//   'features.csv'  list of options and their usage messages
//   'implied.csv'   pairs of options where the first implies the second
//   'clashing.csv'  pairs of incompatible options
//
// The main reason why we do not use shell scripts for generating feature
// files is that we have to compute a transitive hull of implied features.
// This is pretty awkward in a shell script. Furthermore, having a solid
// implementation in C allows better parsing and sanity checking too, i.e.,
// better diagnosis in case something is not as expected (including warnings
// about unsorted features or pairs).  We also have three ways of referring
// to an option, e.g., '--no-block', 'block', and 'NBLOCK', which are used
// in different contexts.

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------*/

#define MAX_FEATURES 64

static char *features[MAX_FEATURES][2];
static const char *options[MAX_FEATURES];
static char *defines[MAX_FEATURES];
static char *names[MAX_FEATURES];
static int size_features;

static size_t max_feature_len;
static size_t max_usage_len;
static const char *max_feature;
static const char *max_usage;

static int clashing[MAX_FEATURES][3];
static int size_clashing;

static int reached[MAX_FEATURES];
static int stack[MAX_FEATURES];
static int size_stack;

static int implied[MAX_FEATURES][3];
static int size_implied;

static int lineno;
static const char *path;
static FILE *input;

static char buffer[1 << 8];
static int size_buffer;

static int directly_implied[MAX_FEATURES][MAX_FEATURES];
static int transitively_implied[MAX_FEATURES][MAX_FEATURES];

static int roots[MAX_FEATURES];
static int size_roots;

static int leafs[MAX_FEATURES];
static int size_leafs;

static int singletons[MAX_FEATURES];
static int size_singletons;

#define MAX_INVALID (MAX_FEATURES * MAX_FEATURES)

static int invalid[MAX_INVALID][2];
static int size_invalid;

static bool verbose;
static bool pedantic;

/*------------------------------------------------------------------------*/

static void die (const char *fmt, ...) __attribute__((format (printf, 1, 2)));

static void message (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

static void parse_error (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

static void parse_warning (const char *fmt, ...)
  __attribute__((format (printf, 1, 2)));

/*------------------------------------------------------------------------*/

static void
die (const char *fmt, ...)
{
  va_list ap;
  fputs ("generate: error: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void
warning (const char *fmt, ...)
{
  va_list ap;
  fputs ("generate: warning", stderr);
  if (pedantic)
    fputs (" treated as error", stderr);
  fputs (": ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  if (pedantic)
    exit (1);
}


static void
message (const char *fmt, ...)
{
  if (!verbose)
    return;
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  fputc ('\n', stdout);
  fflush (stdout);
}

static void
parse_error (const char *fmt, ...)
{
  fprintf (stderr, "generate: parse error: line %d in '%s': ", lineno, path);
  va_list ap;
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static void
parse_warning (const char *fmt, ...)
{
  va_list ap;
  fputs ("generate: parse warning", stderr);
  if (pedantic)
    fputs (" treated as error", stderr);
  fprintf (stderr, ": line %d in '%s': ", lineno, path);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  if (pedantic)
    exit (1);
}

/*------------------------------------------------------------------------*/

static int
find_feature (const char *name)
{
  for (int i = 0; i < size_features; i++)
    if (!strcmp (features[i][0], name))
      return i;
  return -1;
}

/*------------------------------------------------------------------------*/

static bool
read_buffer (char sentinel, bool check)
{
  size_buffer = 0;
  for (;;)
    {
      int ch = getc (input);
      if (ch == EOF)
	{
	  if (size_buffer)
	    parse_error ("unexpected end-of-line");
	  return false;
	}
      if (ch == sentinel)
	{
	  buffer[size_buffer] = 0;
	  if (check && (find_feature (buffer) < 0))
	    parse_error ("feature '%s' not listed in 'features.csv'", buffer);
	  if (ch == '\n')
	    {
	      assert (lineno < INT_MAX);
	      lineno++;
	    }
	  return true;
	}
      if (ch == '\n')
	parse_error ("unexpected new-line");
      if (!isprint (ch))
	parse_error ("non-printable character");
      if (size_buffer + 1 >= sizeof buffer)
	parse_error ("line too large");
      buffer[size_buffer++] = ch;
    }
}

/*------------------------------------------------------------------------*/

static bool
read_feature_usage (char *pair[2])
{
  if (!read_buffer (',', false))
    return 0;
  if (buffer[0] != '-' || buffer[1] != '-' || buffer[2] != 'n' ||
      buffer[3] != 'o' || buffer[4] != '-')
    parse_error ("unsupported option '%s' ('--no-...' prefix expected)",
		 buffer);
  if (0 <= find_feature (buffer))
    parse_error ("duplicated feature '%s'", buffer);
  if (size_features && strcmp (options[size_features - 1], buffer) > 0)
    parse_warning ("feature '%s' unsorted", buffer);
  pair[0] = strdup (buffer);
  if (!read_buffer ('\n', false))
    parse_error ("unexpected end-of-file");
  pair[1] = strdup (buffer);
  return true;
}

static char *
option_to_name (const char *option)
{
  assert (option[0] == '-');
  assert (option[1] == '-');
  assert (option[2] == 'n');
  assert (option[3] == 'o');
  assert (option[4] == '-');
  char *res = malloc (strlen (option) + 1), *q = res, ch;
  for (const char *p = option + 5; (ch = *p); p++)
    if (ch != '-')
      *q++ = ch;
  *q = 0;
  return res;
}

static char *
option_to_define (const char *option)
{
  assert (option[0] == '-');
  assert (option[1] == '-');
  assert (option[2] == 'n');
  assert (option[3] == 'o');
  assert (option[4] == '-');
  char *res = malloc (strlen (option) - 3), *q = res, ch;
  *q++ = 'N';
  for (const char *p = option + 5; (ch = *p); p++)
    if (ch != '-')
      *q++ = toupper (ch);
  *q = 0;
  return res;
}

static void
read_features (void)
{
  lineno = 1;
  input = fopen (path = "features.csv", "r");
  if (!input)
    die ("could not read '%s'", path);
  for (;;)
    {
      char *feature[2];
      if (!read_feature_usage (feature))
	break;
      if (size_features == MAX_FEATURES)
	parse_error ("too many feature usage pairs in '%s'", path);
      features[size_features][0] = feature[0];
      features[size_features][1] = feature[1];
      options[size_features] = feature[0];
      names[size_features] = option_to_name (feature[0]);
      defines[size_features] = option_to_define (feature[0]);
      const size_t feature_len = strlen (feature[0]);
      const size_t usage_len = strlen (feature[1]);
      if (max_feature_len < feature_len)
	max_feature_len = feature_len, max_feature = feature[0];
      if (max_usage_len < usage_len)
	max_usage_len = usage_len, max_usage = feature[1];
      size_features++;
    }
  fclose (input);
  message ("read %d features from '%s'", size_features, path);

  if (max_feature_len + max_usage_len > 74)
    parse_warning ("maximum feature '%s' and maximum usage '%s' too long",
		   max_feature, max_usage);
}

/*------------------------------------------------------------------------*/

static int *
find_pair (int pairs[][3], int size, int pair[3])
{
  for (int i = 0; i < size; i++)
    if (pairs[i][0] == pair[0] && pairs[i][1] == pair[1])
      return pairs[i];
  return 0;
}

/*------------------------------------------------------------------------*/

static bool
read_pair (int pair[3])
{
  if (!read_buffer (',', true))
    return false;
  pair[2] = lineno;
  pair[0] = find_feature (buffer);
  assert (0 <= pair[0]), assert (pair[0] < size_features);
  if (!read_buffer ('\n', true))
    parse_error ("unexpected end-of-file");
  pair[1] = find_feature (buffer);
  assert (0 <= pair[1]), assert (pair[1] < size_features);
  return true;
}

static void
read_pairs (const char *name, int pairs[][3], int *size_ptr)
{
  lineno = 1;
  input = fopen (path = name, "r");
  if (!input)
    die ("could not read '%s'", path);
  for (;;)
    {
      if (*size_ptr == MAX_FEATURES)
	parse_error ("too many feature pairs in '%s'", path);
      if (!read_pair (pairs[*size_ptr]))
	break;
      assert (lineno > 1);
      lineno--;
      int *pair = pairs[*size_ptr];
      int *prev = find_pair (pairs, *size_ptr, pair);
      if (prev)
	parse_error ("pair '%s,%s' already occurs at line %d",
		     options[pair[0]], options[pair[1]], prev[2]);
      int swapped[2] = { pair[1], pair[0] };
      prev = find_pair (pairs, *size_ptr, swapped);
      if (prev)
	parse_error ("pair '%s,%s' occurs already as '%s,%s' at line %d",
		     options[pair[0]], options[pair[1]],
		     options[swapped[0]], options[swapped[1]], prev[2]);
      if (pairs == clashing)
	{
	  if (strcmp (options[pair[0]], options[pair[1]]) >= 0)
	    parse_warning ("features in pair '%s,%s' unsorted",
			   options[pair[0]], options[pair[1]]);
	  int *prev = find_pair (implied, size_implied, pair);
	  if (prev)
	    parse_error ("pair '%s,%s' already in 'implied.csv' at line %d",
			 options[pair[0]], options[pair[1]], prev[2]);
	  prev = find_pair (implied, size_implied, swapped);
	  if (prev)
	    parse_error ("pair '%s,%s' occurs already as '%s,%s' "
			 "in 'implied.csv' at line %d",
			 options[pair[0]], options[pair[1]],
			 options[swapped[0]], options[swapped[1]], prev[2]);
	}
      if (*size_ptr)
	{
	  prev = pairs[*size_ptr - 1];
	  if (strcmp (options[prev[0]], options[pair[0]]) > 0 ||
	      (strcmp (options[prev[0]], options[pair[0]]) == 0 &&
	       strcmp (options[prev[1]], options[pair[1]]) > 0))
	    parse_warning ("pair '%s,%s' unsorted", options[pair[0]],
			   options[pair[1]]);
	}
      lineno++;
      *size_ptr += 1;
    }
  fclose (input);
  message ("read %d feature pairs from '%s'", *size_ptr, path);
}

/*------------------------------------------------------------------------*/

static void
init_directly_implies (void)
{
  for (int i = 0; i < size_implied; i++)
    directly_implied[implied[i][0]][implied[i][1]] = 1;
}

static void
init_roots (void)
{
  for (size_t i = 0; i < size_features; i++)
    {
      int root = 0;
      for (size_t j = 0; j < size_features; j++)
	if (directly_implied[i][j])
	  {
	    root = 1;
	    break;
	  }
      for (size_t j = 0; j < size_features; j++)
	if (directly_implied[j][i])
	  {
	    root = 0;
	    break;
	  }
      if (!root)
	continue;
      message ("root '%s'", options[i]);
      roots[size_roots++] = i;
    }
  message ("found %d roots", size_roots);
}

static void
init_leafs (void)
{
  for (size_t i = 0; i < size_features; i++)
    {
      int leaf = 0;
      for (size_t j = 0; j < size_features; j++)
	if (directly_implied[j][i])
	  {
	    leaf = 1;
	    break;
	  }
      for (size_t j = 0; j < size_features; j++)
	if (directly_implied[i][j])
	  {
	    leaf = 0;
	    break;
	  }
      if (!leaf)
	continue;
      message ("leaf '%s'", options[i]);
      leafs[size_leafs++] = i;
    }
  message ("found %d leafs", size_leafs);
}

static void
init_singletons (void)
{
  for (size_t i = 0; i < size_features; i++)
    {
      int singleton = 1;
      for (size_t j = 0; j < size_features; j++)
	if (directly_implied[i][j] || directly_implied[j][i])
	  {
	    singleton = 0;
	    break;
	  }
      if (!singleton)
	continue;
      message ("singleton '%s'", options[i]);
      singletons[size_singletons++] = i;
    }
  message ("found %d singletons", size_singletons);
}

static void
init_transitively_implies (void)
{
  for (int i = 0; i < size_implied; i++)
    transitively_implied[implied[i][0]][implied[i][1]] = 1;
}

/*------------------------------------------------------------------------*/

static int
check_transitively_implied (int src, int dst, int except)
{
  if (src == dst)
    return 1;
  for (size_t i = 0; i < size_features; i++)
    {
      if ((src != except || i != dst) &&
	  !reached[i] && transitively_implied[src][i])
	{
	  reached[i] = 1;
	  stack[size_stack++] = i;
	  if (check_transitively_implied (i, dst, except))
	    return 1;
	}
    }
  return 0;
}

static void
check_transitive_impliedness (void)
{
  int redundant = 0;
  for (int i = 0; i < size_implied; i++)
    {
      int src = implied[i][0];
      int dst = implied[i][1];
      if (check_transitively_implied (src, dst, src))
	{
	  warning ("implied pair '%s,%s' transitively implied",
		   options[src], options[dst]);
	  redundant++;
	}
      while (size_stack)
	reached[stack[--size_stack]] = 0;
    }
  if (redundant)
    message ("found %d transitively implied pairs", redundant);
  else
    message ("no pair is transitively implied");
}

/*------------------------------------------------------------------------*/

static void
transitive_hull (void)
{
  size_t iterations = 0;
  size_t added = 0;
  bool changed;

  do
    {
      changed = false;
      for (size_t i = 0; i < size_features; i++)
	for (size_t j = 0; j < size_features; j++)
	  for (size_t k = 0; k < size_features; k++)
	    if (transitively_implied[i][j] && transitively_implied[j][k] &&
		!transitively_implied[i][k])
	      {
		transitively_implied[i][k] = 1;
		changed = true;
		added++;
	      }
      iterations++;
    }
  while (changed);

  message ("computed transitive hull of 'implied'");
  message ("added %zu implications in %zu iterations", added, iterations);
}

/*------------------------------------------------------------------------*/

static void
check_cyclic_dependencies (void)
{
  for (size_t i = 0; i < size_features; i++)
    if (transitively_implied[i][i])
      warning ("option '%s' implies itself recursively", options[i]);

  message ("no options depends on itself recursively");
}

/*------------------------------------------------------------------------*/

static void
check_clashing_not_transitively_implied (void)
{
  for (int i = 0; i < size_clashing; i++)
    {
      int *pair = clashing[i];
      lineno = pair[2];
      assert (path), assert (!strcmp (path, "clashing.csv"));
      if (transitively_implied[pair[0]][pair[1]])
	parse_error ("pair '%s,%s' transitively implied",
		     options[pair[0]], options[pair[1]]);
      else if (transitively_implied[pair[1]][pair[0]])
	parse_error ("pair '%s,%s' reverse transitively implied",
		     options[pair[0]], options[pair[1]]);
    }
}

/*------------------------------------------------------------------------*/

static void
push_invalid_pair (int i, int j)
{
  assert (size_invalid < MAX_INVALID);
  if (strcmp (names[i], names[j]) > 0)
    {
      int tmp = i;
      i = j;
      j = tmp;
    }
  invalid[size_invalid][0] = i;
  invalid[size_invalid][1] = j;
  size_invalid++;
}

static int
cmp_invalid (const void *a, const void *b)
{
  const int *p = (const int *) a;
  const int *q = (const int *) b;
  int res = strcmp (names[p[0]], names[q[0]]);
  if (!res)
    res = strcmp (names[p[1]], names[q[1]]);
  return res;
}

static void
sort_invalid_feature_pairs (void)
{
  for (int i = 0; i < size_features; i++)
    for (int j = 0; j < size_features; j++)
      if (transitively_implied[i][j])
	push_invalid_pair (i, j);
  for (int i = 0; i < size_clashing; i++)
    push_invalid_pair (clashing[i][0], clashing[i][1]);
  qsort (invalid, size_invalid, sizeof *invalid, cmp_invalid);
  message ("sorted %d invalid pairs", size_invalid);
}

/*------------------------------------------------------------------------*/

static FILE *
write_file (const char *name)
{
  FILE *file = fopen (name, "w");
  if (!file)
    die ("could not write '%s'", name);
  return file;
}

static void
close_file (FILE * file, const char *name)
{
  if (fclose (file))
    die ("could not close '%s'", name);
  message ("generated '%s'", name);
}

/*------------------------------------------------------------------------*/

static FILE *
write_shell (const char *name)
{
  FILE *file = write_file (name);
  fputs ("# Automatically generated by 'features/generate'.\n", file);
  return file;
}

static FILE *
write_header (const char *name)
{
  FILE *file = write_file (name);
  fputs ("// Automatically generated by 'features/generate'.\n", file);
  return file;
}

/*------------------------------------------------------------------------*/

static void
generate_init_sh (void)
{
  const char *name = "init.sh";
  FILE *file = write_shell (name);
  fputs ("\n# Initialize all features to be enabled by default.\n\n", file);
  for (int i = 0; i < size_features; i++)
    fprintf (file, "%s=yes\n", names[i]);
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

static void
generate_only_sh (void)
{
  const char *name = "only.sh";
  FILE * file = write_shell (name);
  fputs ("\n# Handle '--only-<feature>' options.\n\n", file);
  for (int i = 0; i < size_features; i++)
    {
    }
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

static void
generate_parse_sh (void)
{
  const char *name = "parse.sh";
  FILE *file = write_shell (name);
  fputs ("\n# Match options which disable features.\n\n", file);
  // *INDENT-OFF*
  fputs ("parse () {\n"
         "  res=0\n"
         "  case x\"$1\" in\n", file);
  for (int i = 0; i < size_features; i++)
    fprintf (file, "    x\"%s\") %s=no;;\n", options[i], names[i]);
  fputs ("    *) res=1;;\n"
         "  esac\n"
         "  return $res\n"
         "}\n", file);
  // *INDENT-ON*
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

static void
check_sh_implied (FILE * file, int i, int j)
{
  fprintf (file, "[ $%s = no -a $%s = no ] && die \"'%s' implies '%s'\"\n",
	   names[i], names[j], options[i], options[j]);
}

static void
check_sh_clashing (FILE * file, int i, int j)
{
  fprintf (file,
	   "[ $%s = no -a $%s = no ] && "
	   "die \"can not combine '%s' and '%s'\"\n",
	   names[i], names[j], options[i], options[j]);
}

static void
generate_check_sh (void)
{
  const char *name = "check.sh";
  FILE *file = write_shell (name);
  fputs ("\n# Check implied disabled features are not disabled.\n\n", file);
  for (int i = 0; i < size_features; i++)
    for (int j = 0; j < size_features; j++)
      if (transitively_implied[i][j])
	check_sh_implied (file, i, j);
  fputs ("\n# Check clashing disabled features.\n\n", file);
  for (int i = 0; i < size_clashing; i++)
    check_sh_clashing (file, clashing[i][0], clashing[i][1]);
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

static void
generate_usage_sh (void)
{
  const char *name = "usage.sh";
  FILE *file = write_shell (name);
  fputs ("\n# Print option usage to disable features.\n\n", file);
  fputs ("cat<<EOF\n", file);
  char fmt[16];
  sprintf (fmt, "%%-%zus %%s\n", max_feature_len);
  for (int i = 0; i < size_features; i++)
    fprintf (file, fmt, features[i][0], features[i][1]);
  fputs ("EOF\n", file);
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

static void
generate_define_sh (void)
{
  const char *name = "define.sh";
  FILE *file = write_shell (name);
  fputs ("\n# Compiler definitions to disable features.\n\n", file);
  for (int i = 0; i < size_features; i++)
    fprintf (file, "[ $%s = no ] && CFLAGS=\"$CFLAGS -D%s\"\n",
	     names[i], defines[i]);
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

static void
generate_version_h (void)
{
  const char *name = "version.h";
  FILE *file = write_header (name);
  fputs ("\n// Version extension string for disabled features.\n\n", file);
  for (int i = 0; i < size_features; i++)
    fprintf (file, "#ifdef %s\n\"-%s\"\n#endif\n", defines[i], names[i]);
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

// *INDENT-OFF*

static void
check_h_implied (FILE * file, int i, int j)
{
  fprintf (file,
    "#if defined(%s) && defined(%s)\n"
    "#error \"'%s' implies '%s' (the latter should not be defined)\"\n"
    "#endif\n",
    defines[i], defines[j], defines[i], defines[j]);
}

static void
check_h_clashing (FILE * file, int i, int j)
{
  fprintf (file,
    "#if defined(%s) && defined(%s)\n"
    "#error \"'%s' and '%s' can not be combined\"\n"
    "#endif\n",
    defines[i], defines[j], defines[i], defines[j]);
}

// *INDENT-ON*

static void
generate_check_h (void)
{
  const char *name = "check.h";
  FILE *file = write_header (name);
  fputs ("\n// Check implied disabled features are not disabled.\n\n", file);
  for (int i = 0; i < size_features; i++)
    for (int j = 0; j < size_features; j++)
      if (transitively_implied[i][j])
	check_h_implied (file, i, j);
  fputs ("\n// Check clashing disabled features.\n\n", file);
  for (int i = 0; i < size_clashing; i++)
    check_h_clashing (file, clashing[i][0], clashing[i][1]);
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

// *INDENT-OFF*

static void
init_h_implied (FILE * file, int i, int j)
{
  fprintf (file,
    "#if defined(%s) && !defined(%s)\n"
    "#define %s\n"
    "#endif\n",
    defines[i], defines[j], defines[j]);
}

// *INDENT-ON*

static void
generate_init_h (void)
{
  const char *name = "init.h";
  FILE *file = write_header (name);
  fputs ("\n// Force implied disabled features to be disabled.\n\n", file);
  for (int i = 0; i < size_features; i++)
    for (int j = 0; j < size_features; j++)
      if (transitively_implied[i][j])
	init_h_implied (file, i, j);
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

static void
generate_list_h (void)
{
  const char *name = "list.h";
  FILE *file = write_header (name);
  fputs ("\n// List of features.\n\n", file);
  for (int i = 0; i < size_features; i++)
    fprintf (file, "\"%s\",\n", options[i]);
  close_file (file, name);
}

static void
generate_invalid_h (void)
{
  const char *name = "invalid.h";
  FILE *file = write_header (name);
  fputs ("\n// Pairs of invalid features.\n\n", file);
  for (int i = 0; i < size_invalid; i++)
    fprintf (file, "\"%s\", \"%s\",\n",
	     options[invalid[i][0]], options[invalid[i][1]]);
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

// *INDENT-OFF*

static void
diagnose_h_message (FILE * file, int i)
{
  fprintf (file,
    "#ifdef %s\n"
    "#pragma message \"#define %s\"\n"
    "#endif\n",
    defines[i], defines[i]);
}

// *INDENT-ON*

static void
generate_diagnose_h (void)
{
  const char *name = "diagnose.h";
  FILE *file = write_header (name);
  fputs ("\n// Print compile time diagnostics on disabled features.\n\n",
	 file);
  for (int i = 0; i < size_features; i++)
    diagnose_h_message (file, i);
  close_file (file, name);
}

/*------------------------------------------------------------------------*/

typedef void (*generator) (void);

struct named_generator
{
  const char *name;
  generator function;
};

static struct named_generator generators[] = {
  {"init.sh", generate_init_sh},
  {"only.sh", generate_only_sh},
  {"parse.sh", generate_parse_sh},
  {"usage.sh", generate_usage_sh},
  {"check.sh", generate_check_sh},
  {"define.sh", generate_define_sh},
  {"version.h", generate_version_h},
  {"check.h", generate_check_h},
  {"init.h", generate_init_h},
  {"list.h", generate_list_h},
  {"invalid.h", generate_invalid_h},
  {"print.h", generate_diagnose_h},
};

#define size_generators (sizeof generators / sizeof *generators)

static generator
find_generator (const char *name)
{
  for (size_t i = 0; i < size_generators; i++)
    if (!strcmp (generators[i].name, name))
      return generators[i].function;
  return 0;
}

static void
list_generators (void)
{
  for (size_t i = 0; i < size_generators; i++)
    printf ("%s\n", generators[i].name);
}

static void
generate_all (void)
{
  for (size_t i = 0; i < size_generators; i++)
    generators[i].function ();
}

/*------------------------------------------------------------------------*/

static void
release_features (void)
{
  for (int i = 0; i < size_features; i++)
    {
      free (features[i][0]);
      free (features[i][1]);
      free (defines[i]);
      free (names[i]);
    }
}

/*------------------------------------------------------------------------*/

int
main (int argc, char **argv)
{
  bool all = false;
  int found = 0;
  for (int i = 1; i < argc; i++)
    {
      const char *arg = argv[i];
      if (!strcmp (arg, "-h"))
	{
	  printf ("%s", usage);
	  exit (0);
	}
      else if (!strcmp (arg, "-v"))
	verbose = true;
      else if (!strcmp (arg, "-p"))
	pedantic = true;
      else if (!strcmp (arg, "-l"))
	{
	  list_generators ();
	  exit (0);
	}
      else if (!strcmp (arg, "all"))
	{
	  if (all)
	    die ("multiple 'all' options");
	  all = true;
	}
      else if (!find_generator (arg))
	die ("can not generate '%s' (try '-l')", arg);
      else if (all)
	die ("can use both 'all' and '%s'", arg);
      else
	found++;
    }

  read_features ();
  read_pairs ("implied.csv", implied, &size_implied);
  read_pairs ("clashing.csv", clashing, &size_clashing);

  init_directly_implies ();

  init_roots ();
  init_leafs ();
  init_singletons ();

  init_transitively_implies ();
  check_transitive_impliedness ();
  transitive_hull ();

  check_cyclic_dependencies ();
  check_clashing_not_transitively_implied ();
  sort_invalid_feature_pairs ();

  if (!found || all)
    {
      message ("generating all files");
      generate_all ();
    }
  else
    {
      for (int i = 1; i < argc; i++)
	{
	  const char *arg = argv[i];
	  if (arg[0] == '-')
	    continue;
	  generator generate = find_generator (arg);
	  assert (generate);
	  generate ();
	}
    }

  release_features ();

  return 0;
}
