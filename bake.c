/* @BAKE cc -std=c99 -O2 -Wall -Wextra -Wpedantic -Wno-implicit-fallthrough -o @SHORT @FILENAME @ARGS @STOP */
/* @BAKE cc -std=c99 -O2 -Wall -Wextra -Wpedantic -Wno-implicit-fallthrough -o '@{\} @{\}@SHORT\}}' @FILENAME @ARGS @STOP */
/* @BAKE cc -std=c99 -O2 -Wall -Wextra -Wpedantic -Wno-implicit-fallthrough -o '@{\}}' @FILENAME @ARGS @STOP */
/* @BAKE cc -std=c99 -O2 -Wall -Wextra -Wpedantic -Wno-implicit-fallthrough -o '@{a}' @FILENAME @ARGS @STOP */
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_SIZE (1 << 12)

#define START "@" "BAKE" " "
#define STOP  "@" "STOP"

#define AUTONOMOUS_COMPILE 0

#define ENABLE_COLOR 1

#if ENABLE_COLOR == 1
# define    RED "\033[91m"
# define  GREEN "\033[92m"
# define YELLOW "\033[93m"
# define   BOLD "\033[1m"
# define  RESET "\033[0m"
#elif ENABLE_COLOR
# define    RED "\033[91;5m"
# define  GREEN "\033[96;7m"
# define YELLOW "\033[94m"
# define   BOLD "\033[1;4m"
# define  RESET "\033[0m"
#else
# define    RED
# define  GREEN
# define YELLOW
# define   BOLD
# define  RESET
#endif

#define ARRLEN(x) (sizeof (x) / sizeof (x [0]))

__attribute__((__section__(".text"))) static volatile char autonomous_compile [] =
  "@BAKE cc -std=c99 -O2 -Wall -Wextra -Wpedantic -Wno-implicit-fallthrough -o @SHORT @FILENAME.c @ARGS @STOP";

static void
swap (char * a, char * b) {
  *a ^= *b;
  *b ^= *a;
  *a ^= *b;
}

static int
root (char ** rootp) {
  char x [1] = {'\0'};
  char * root = *rootp;
  size_t len = strlen (root);
  int ret;
  while (len && root [len] != '/') { --len; }
  if (!len) { return 0; }
  swap (root + len, x);
  ret = chdir (root);
  swap (root + len, x);
  *rootp += len + 1;
  return ret;
}

static char *
shorten (char * filename) {
  size_t i, last, len;
  static char sh [FILENAME_MAX];
  len = strlen (filename);
  for (last = i = 0; i < len; ++i) {
    if (filename [i] == '.') { last = i; }
  }
  last = last ? last : i;
  strncpy (sh, filename, last);
  sh [last] = '\0';
  return sh;
}

static char *
all_args (size_t argc, char ** argv) {
  static char buffer [BUFFER_SIZE] = {0};
  if (argc > 0) {
    size_t i, len = argc;
    for (i = 0; i < argc; ++i) { len += strlen (argv [i]); }
    buffer [len] = '\0';
    for (len = 0, i = 0; i < argc; ++i) {
      strcpy (buffer + len, argv [i]);
      len += strlen (argv [i]) + 1;
      if (i + 1 < argc) { buffer [len - 1] = ' '; }
  }}
  return buffer;
}

static size_t
lines (char * buffer, size_t off) {
  size_t line = 1;
  char * end = buffer + off;
  while (buffer < end) {
    if (*buffer == '\n') { ++line; }
    ++buffer;
  }
  return line;
}

static char *
expand_buffer(char * expanded, char * buffer, size_t length, char ** pairs, size_t count) {
  size_t old, new, i, f, off = 0;
  /* I need to test the bounds checking here, it'll crash normally if this provision doesn't do anything, though. */
  length &= (1 << 12) - 1;
  for (f = 0; f < length; ++f) {
    for (i = 0; i < count; i += 2) {
      old = strlen (pairs [i]);
      new = strlen (pairs [i + 1]);
      if (memcmp (buffer + f - off, pairs [i], old) == 0) {
        if (f && buffer [f - off - 1] == '\\')
        { --f; --off; break; }
        memcpy (expanded + f, pairs [i + 1], new);
        f += new;
        length += new - old;
        off += new - old;
        break;
    }}
    expanded [f] = buffer [f - off];
  }
  expanded [f] = '\0';
  return expanded;
}

static char *
expand (char * buffer, size_t length, char ** pairs, size_t count) {
  static char expanded [BUFFER_SIZE] = {0};
  return expand_buffer (expanded, buffer, length, pairs, count);
}

static char *
expunge (char * to, char * from, size_t length, char ** pair, size_t count) {
  size_t i, f;
  (void)count;                  /* count isn't actually used... */
  static char expunge_me [FILENAME_MAX] = {0};
  for (i = 0; i < length; ++i) {
    if (memcmp(from + i, pair[0], strlen(pair[0])) == 0) {
      if (from[i - 1] == '\\') { --i; memmove(from + i, from + i + 1, length - i); i+=1+strlen(pair[0]); --length; continue; }
      for (f = i + strlen(pair[0]); f < length; ++f) {
        if (memcmp(from + f, pair[1], strlen(pair[1])) == 0) {
          if (from[f - 1] == '\\') { --f; memmove(from + f, from + f + 1, length - f); --length; continue; }
          memmove(to + i, from + i + strlen(pair[0]), length - i - strlen(pair[0]));
          memmove(to + f - strlen(pair[1]), from + f - 1, length - f);
          /* memcpy(to + f - 2, "        ", strlen(pair[1])); */
          memcpy(expunge_me, to + i, f - i - 1 - strlen(pair[1]));
          goto end;
        }
      }
    }
  }
end:
  return expunge_me;
}

static char *
getmap (char * filename, size_t * length) {
  char * buffer = NULL;
  int fd = open (filename, O_RDONLY);
  if (fd != -1) {
    struct stat s;
    if (!fstat (fd, &s)
        &&   s.st_mode & S_IFREG
        &&   s.st_size) {
      *length = (size_t) s.st_size;
      buffer = (char *) mmap (NULL, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
    }
    close (fd);
  }
  return buffer;
}

# define color_printf(...) color_fprintf (stdout, __VA_ARGS__)
/* not perfect, too simple, doesn't work with a var, only a literal. */
# define color_fputs(fp, msg) color_fprintf (fp, msg "\n")
# define color_puts(msg) color_fputs (stdout, msg)

static int color = ENABLE_COLOR;

static void
color_fprintf (FILE * fp, char * format, ...) {
  va_list ap;
  char * buf;

  va_start (ap, format);

  if (color) {
    vfprintf (fp, format, ap);
    va_end (ap);
    return;
  }
  vasprintf (&buf, format, ap);

  if (buf) {
    char * expanded, * colors [] = {
      YELLOW, "",
      GREEN,  "",
      RED,    "",
      BOLD,   "",
      RESET,  ""
    };
    size_t count = ARRLEN (colors);
    expanded = expand (buf, strlen (buf), colors, count - 2);
    expanded = expand (expanded, strlen (buf), colors + count - 2, 2);
    fwrite (expanded, strlen (expanded), 1, fp);
  }

  free (buf);
  va_end (ap);
}

/* -- */

int main (int argc, char ** argv) {
  void help (void);
  int off, run = 1, list = 0, ex = 0, select_input, select = 1;
  size_t length, begin = 0, end = 0;
  pid_t pid;
  char   line [10], expanded [BUFFER_SIZE], * buffer, * expandedp, * args, * shortened, * filename = NULL,
       * paren [] = { "@{", "}" }, * expunge_me = NULL, * bake_begin = START, * bake_end = STOP;
  
  /* opts */
  for (off = 1; off < argc; ++off) {
    if (argv [off][0] != '-') { filename = argv [off]; ++off; break; }
    if (argv [off][1] != '-') {
      while (*(++argv [off]))
      switch (argv [off][0]) {
      select:  case 's':
        select = atoi (argv [off] + 1);
        if (!select) {
          ++off;
          if (off >= argc) { help (); }
          select = atoi (argv [off]);
        }
        if (select) { goto next; }
      default: case 'h': help ();
               case 'l': list = 1;
               case 'n': run = 0;   break;
               case 'c': color = 0; break;
               case 'x': ex = 1; break;
               case 'q': fclose(stderr); fclose(stdout); break;
      }
      continue;
    }
    argv[off] += 2;
         if (strcmp(argv[off],  "select") == 0) { goto select; }
    else if (strcmp(argv[off],    "list") == 0) { list = 1; run = 0; }
    else if (strcmp(argv[off], "dry-run") == 0) { run = 0; }
    else if (strcmp(argv[off],   "color") == 0) { color = 0; }
    else if (strcmp(argv[off], "expunge") == 0) { ex = 1; }
    else if (strcmp(argv[off],   "quiet") == 0) { fclose(stderr); fclose(stdout); }
    else if (strcmp(argv[off],    "help") == 0) { help(); }
  next:;
  }
  if (argc == 1 || !filename) { help (); }
  select_input = select;
  /* roots to directory of filename and mutates filename with the change */
  root (&filename);

  buffer = getmap (filename, &length);
  if (!buffer) {
    color_fprintf (stderr, RED "%s" RESET ": Could not access '" BOLD "%s" RESET "'\n", argv [0], filename);
    return 1;
  }
  for (begin = 0; (list || select) && begin < length - strlen (bake_begin); ++begin) {
    if (memcmp (buffer + begin, bake_begin, strlen (bake_begin)) == 0) {
      size_t stop = begin;
      end = begin;
      again: while (end < length && buffer[end] != '\n') { ++end; }
      if (buffer[end - 1] == '\\') { ++end; goto again; }
      while (stop < length - strlen(bake_end)) {
        if (memcmp(buffer + stop, bake_end, strlen(bake_end))  == 0) {
          if (stop && buffer[stop - 1] == '\\') { ++stop; continue; }
          end = stop;
          break;
        }
        ++stop;
      }
      if (list) {
        color_printf (GREEN "%d,%d" RESET ": " BOLD, select++, lines (buffer, begin));
        fwrite (buffer + begin, 1, end - begin, stdout);
        color_puts (RESET);
      } else { --select; }
  }}
  begin += strlen (bake_begin) - 1;

  if (list) { return 0; }
  if (end < begin) {
    color_fprintf (stderr, RED "%s" RESET ": " BOLD "%d" RESET " is out of range\n", argv [0], select_input);
    return 1;
  }

  /* expansion */

  args = all_args (argc - off, argv + off);
  shortened = shorten (filename);
  char * pair [] = {
      "@FILENAME",  filename,
      "@FILE",      filename,
      "$@",         filename,
      "@SHORT",     shortened,
      "$*",         shortened,
      "@ARGS",      args,
      "$+",         args,
      "@LINE",      line
  };
  snprintf (line, 16, "%lu", lines (buffer, begin));
  expandedp = expand (buffer + begin, end - begin, pair, ARRLEN (pair));
  memcpy(expanded, expandedp, BUFFER_SIZE);
  expunge_me = expunge (expanded, expanded, strlen(expanded), paren, ARRLEN (paren));
  munmap (buffer, length);

  /* print and execute */
  color_fprintf (stderr, GREEN "%s" RESET ": " BOLD "%s" RESET, argv [0], expanded);
  if (expanded[strlen(expanded)] != '\n') { puts(""); }

  if (ex) {
    color_fprintf (stderr, GREEN "%s" RESET ": removing '%s'\n", argv [0], expunge_me);
    char * jin = ".c";
    if (expunge_me
    &&  run
    &&  /* just in case */
        memcmp(expunge_me + strlen(expunge_me) - strlen(jin), jin, strlen(jin)) != 0)
    { remove(expunge_me); }
    return 0;
  }

  if (!run) { return 0; }

  color_fprintf (stderr, GREEN "output" RESET ":\n");
  fflush(stdout);

  if ((pid = fork ()) == 0) {
    execl ("/bin/sh", "sh", "-c", expanded, NULL);
    return 0; /* execl overwrites the process anyways */
  }

  if (pid == -1) {
    color_fprintf (stderr, GREEN "%s" RESET ": %s, %s\n",
            argv [0], "Fork Error", strerror (errno));
    return 1;
  }

  /* reuse of run as status return */
  if (waitpid (pid, &run, 0) < 0) {
    color_fprintf (stderr, GREEN "%s" RESET ": " RED "%s" RESET ", %s\n",
            argv [0], "Wait PID Error", strerror (errno));
    return 1;
  }
  if (!WIFEXITED (run)) {
    return 1;
  }
  return WEXITSTATUS (run);
}

void help (void) {
  char * help =
    BOLD "bake [-chln] [-s <n>] <FILENAME> [ARGS...]; version 20240804\n\n" RESET
    GREEN "Bake" RESET " is a simple tool meant to execute embedded shell commands within\n"
    "any file.  It executes with /bin/sh the command after a \"" BOLD "@BAKE" RESET " \" to\n"
    "the end of the line (a UNIX newline: '\\n').\n\n"
    "It expands some macros,\n"
    YELLOW "\t@NAME   " RESET "- filename\n"
    YELLOW "\t@SHORT  " RESET "- shortened filename\n"
    YELLOW "\t@ARGS   " RESET "- other arguments to the program\n"
    YELLOW "\t@LINE   " RESET "- line number at the selected " BOLD "@BAKE" RESET "\n\n"
    "All macros can be exempted by prefixing them with a backslash,\n"
    "which'll be subtracted in the expansion. multi-line commands may be\n"
    "done by a leading backslash, which are NOT subtracted.\n\n"
    "It has five options, this message (-h, --help); prevents the execution\n"
    "of the shell command (-n, --dry-run); disable color (-c, --color); list\n"
    "(-l, --list) and select (-s<n>, --select <n>) which respectively lists\n"
    "all " BOLD "@BAKE" RESET " commands and select & run the Nth command.\n\n"
    "It roots the shell execution in the directory of the given file.\n"
    "Expunge with @{filename...}, using (-x, --expunge), \\@{} or @{\\}}.\n\n"
    "Licensed under the public domain.\n";
  color_printf ("%s", help);
  exit (1);
}
