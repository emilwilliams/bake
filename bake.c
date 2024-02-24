/* bake.c - Ever burned a cake?
 * Copyright 2023 Emil Williams
 *
 * Licensed under the GNU Public License version 3 only, see LICENSE.
 *
 * @BAKE cc -std=c89 -O2 -I. $@ -o $* $+ # @STOP
 */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"

#define  HELP                                                                                         \
  BOLD "[option] target-file" RESET " [" GREEN "arguments" RESET " ...]\n"                            \
  "Use the format `" BOLD "@BAKE" RESET " cmd ...' within the target-file, this will execute the\n"   \
  "rest of line, or if found within the file, until the " BOLD "@STOP" RESET " marker.\n"             \

#define DESC                                                 \
  "Options [Must always be first]\n"                         \
  "\t" DIM "-h --help" RESET", " BOLD "-n --dry-run\n" RESET \
  "Expansions\n"                                             \
  "\t" YELLOW "$@" RESET "  returns target-file                (abc.x.txt)\n"  \
  "\t" YELLOW "$*" RESET "  returns target-file without suffix (^-> abc.x)\n"  \
  "\t" YELLOW "$+" RESET "  returns " GREEN "arguments" RESET "\n"

typedef struct {
  size_t len;
  char * buf;
} string_t;

const static string_t START = { 5, "@BAKE" };
const static string_t STOP  = { 5, "@STOP" };

/*** Utility functions ***/

static void
swap(char * a, char * b) {
  *a ^= *b;
  *b ^= *a;
  *a ^= *b;
}

static char *
find(string_t x, char * buf, char * end) {
  for (; (buf < end) && x.len < (size_t)(end - buf); ++buf) {
    if (!strncmp(buf, x.buf, x.len))
    { return buf; }
  }
  return NULL;
}

static char *
insert(char * new, size_t len, char * str, size_t slen, size_t offset, size_t shift) {
  size_t max;
  max = (slen + 1 - offset - shift);
  memmove(str + offset + len, str + offset + shift, max);
  memcpy(str + offset, new, len);
  return str;
}

/*** g_short, g_all Functions ***/

#define GLOBALS_COUNT 3

static char * globals[GLOBALS_COUNT];

#define g_filename globals[0]
#define    g_short globals[1]
#define      g_all globals[2]

/* doing a ? strlen(a) : "" really bothered me and this could be optimized out */
static size_t *
get_globals_length(void) {
  static size_t len[GLOBALS_COUNT];
  size_t i;
  for (i = 0; i < GLOBALS_COUNT; ++i) {
    len[i] = strlen(globals[i]);
  }
  return len;
}

static char *
shorten(char * fn) {
  size_t i, last = 0, len;
  char * sh;
  len = strlen(fn);
  sh = malloc(len + 1);
  if (!sh) { return NULL; }
  for (i = 0; i < len; ++i) {
    if (fn[i] == '.') { last = i; }
  }
  last = last ? last : i;
  strncpy(sh, fn, last);
  sh[last] = '\0';
  return sh ? sh : calloc(1,1);
}

static char *
all_args(size_t argc, char ** argv) {
  char * all = NULL;
  if (argc > 2) {
    size_t i, len = argc;

    for (i = 2; i < argc; ++i)
    { len += strlen(argv[i]); }

    all = malloc(len + 1);
    if (!all) { return NULL; }

    all[len] = '\0';
    for (len = 0, i = 2; i < argc; ++i) {
      strcpy(all + len, argv[i]);
      len += strlen(argv[i]) + 1;
      if (i + 1 < argc) { all[len - 1] = ' '; }
    }
  }
  return all ? all : calloc(1,1);
}

/*** Map ***/

typedef struct {
  char * str;
  size_t len;
} map_t;

static map_t
map(char * fn) {
  struct stat s;
  int fd;
  map_t m = (map_t) {0};
  fd = open(fn, O_RDONLY);
  if (fd != -1) {
    if (!fstat(fd,&s)
    &&   s.st_mode & S_IFREG
    &&   s.st_size) {
      m.len = (size_t) s.st_size;
      m.str = (char *) mmap(NULL, m.len, PROT_READ, MAP_SHARED, fd, 0);
    }
    close(fd);
  }
  return m;
}

/*** Important Functions ***/

static char *
find_region(map_t m, size_t * retlen) {
  extern char * strndup(const char * s, size_t n); /* for splint */
  char * buf = NULL, * start, * stop;

  start = find(START, m.str, m.str + m.len);

  if (start) {
    start += START.len;

#ifdef REQUIRE_SPACE
    if (!isspace(*start)) {
      fprintf(stderr, RED "%s" RESET ": Found start without suffix spacing.\n", g_filename);
      return buf;
    }
#endif /* REQUIRE_SPACE */

    stop = find(STOP, start, start + m.len - (start - m.str));

    if (!stop) {
      stop = start;
      while (*stop && *stop != '\n') {
        if (stop[0] == '\\'
        &&  stop[1] == '\n') { stop += 2; }
        ++stop;
      }
    }

    if (stop)
    {
      *retlen = (size_t) (stop - m.str) - (start - m.str);
      buf = strndup(start, *retlen);
    }
  }
  return buf;
}

static int
root(char ** rootp) {
  char x[1] = {'\0'};
  char * root = *rootp;
  size_t len = strlen(root);
  int ret;

  while (len && root[len] != '/')
  { --len; }

  if (!len) { return 0; }

  swap(root + len, x);
  ret = chdir(root);
  swap(root + len, x);

  *rootp += len + 1;
  return ret;
}

#define ARRLEN(x) (sizeof(x) / sizeof(x[0]))

static char *
expand(char * buf, size_t len) {
  enum {
    MACRO_FILENAME = 0,
    MACRO_SHORT    = 1,
    MACRO_ARGS     = 2,
  };

  char *macro[] = {
    [MACRO_FILENAME] = "$@",
    [MACRO_SHORT   ] = "$*",
    [MACRO_ARGS    ] = "$+",
  };

  size_t macro_length[] = {
    [MACRO_FILENAME] = 2,
    [MACRO_SHORT   ] = 2,
    [MACRO_ARGS    ] = 2,
  };

  size_t * globals_length = get_globals_length();

  char * ptr;
  size_t i, f;

  {
    size_t max = len + 1;
    for (i = 0; i < len; ++i) {
      for (f = 0; f < ARRLEN(macro); ++f) {
        if (!strncmp(buf + i, macro[f], macro_length[f])) {
          max += globals_length[f];
        }
      }
    }
    buf = realloc(buf, max + 10);
    len = max - 1;
  }
  for (i = 0; i < len; ++i) {
    for (f = 0; f < ARRLEN(macro); ++f) {
      if (!strncmp(buf + i, macro[f], macro_length[f])) {
        buf = insert(globals[f], globals_length[f], buf, len, i, 2);
      }
    }
  }
  return buf;
}

/* Strips all prefixing and leading whitespace.
 * Except if the last character beforehand is a newline. */
static size_t
strip(char * buf) {
  size_t i = strlen(buf);

  if (!i)
  { return 0; }

  while (isspace(buf[i - 1]))
  { --i; }

  buf[i] = '\0';
  for (i = 0; isspace(buf[i]); ++i);

  return i - (buf[i - 1] == '\n');
}

static int
run(char * buf) {
  fputs(GREEN "output" RESET ":\n", stderr);
  return system(buf);
}

int
main(int argc, char ** argv) {
  int ret = 0;
  char * buf = NULL;
  size_t len;

  if (argc < 2
  ||  !strcmp(argv[1], "-h")
  ||  !strcmp(argv[1], "--help"))
  { goto help; }

  g_filename = argv[1];

  if (!strcmp(argv[1], "-n")
  ||  !strcmp(argv[1], "--dry-run")) {
    if (argc > 2) { ret = 1; g_filename = argv[2]; }
    else { goto help; }
  }

  { map_t m = map(g_filename);
    if (m.str) {
      buf = find_region(m, &len);
      munmap(m.str, m.len);
    }
  }

  if (!buf) {
    if (errno)
    { fprintf(stderr, BOLD RED "%s" RESET ": %s\n", g_filename, strerror(errno)); }
    else
    { fprintf(stderr, BOLD RED "%s" RESET ": File unrecognized.\n", g_filename); }
    return 1;
  }

  g_all = all_args((size_t) argc, argv);
  g_short = shorten(g_filename);
  root(&g_filename);
  buf = expand(buf, len);
  free(g_short); free(g_all);
  if (!buf) { return 1; }

  fprintf(stderr, GREEN "%s" RESET ": %s\n", argv[0], buf + strip(buf));
  ret = ret ? 0 : run(buf);
  if (ret)
  { fprintf(stderr, RED "result" RESET ": " BOLD "%d\n" RESET, ret); }

  free(buf);
  return ret;
help:
  fprintf(stderr, YELLOW "%s" RESET ": %s", argv[0], HELP DESC);
  return 1;
}
