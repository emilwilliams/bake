/* bake.c - Ever burned a cake?
 * Copyright 2023 Emil Williams
 *
 * Licensed under the GNU Public License version 3 only, see LICENSE.
 *
 * @BAKE cc -std=c99 -O2 $@ -o $* $+ # @STOP
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
#include <sys/wait.h>
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

typedef string_t map_t;

static string_t START = { 5, "@BAKE" };
static string_t STOP  = { 5, "@STOP" };

/*** Utility functions ***/

#define strneq(a,b,n) (!strncmp(a,b,n))
#define streq(a,b) (!strcmp(a,b))

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

static void
insert(char * str, size_t slen, char * new, size_t len, size_t shift) {
  memmove(str + len, str + shift, slen - len - shift);
  memcpy(str, new, len);
}

/*** g_short, g_all Functions ***/

#define GLOBALS_COUNT 3

static string_t globals[GLOBALS_COUNT];

#define g_filename globals[0]
#define    g_short globals[1]
#define      g_all globals[2]

static string_t
shorten(string_t s) {
  size_t i, last = 0, len = s.len;
  char * sh, * fn = s.buf;
  sh = malloc(len);
  if (sh) {
    for (i = 0; i < len; ++i) {
      if (fn[i] == '.') { last = i; }
    }
    last = last ? last : i;
    strncpy(sh, fn, last);
    sh[last] = '\0';
  }
  return (string_t) { last, sh ? sh : calloc(0,0) };
}

static string_t
all_args(int argc, char ** argv) {
  char * all = NULL;
  size_t len = 0;
  if (argc > 2) {
    int i;
    len = (size_t) argc;

    for (i = 2; i < argc; ++i)
    { len += strlen(argv[i]); }

    all = malloc(len);
    if (all) {
      all[len - 1] = '\0';
      for (len = 0, i = 2; i < argc; ++i) {
        strcpy(all + len, argv[i]);
        len += strlen(argv[i]);
        len += (i + 1 < argc);
        if (i + 1 < argc) { all[len - 1] = ' '; }
      }
    }
  }
  return (string_t) { len, all ? all : calloc(0,0) };
}

#if 0
static string_t
all_args(int argc, char ** argv) {
  string_t s = (string_t) { 0, NULL };
  if (argc > 2) {
    size_t i;
    for (i = (size_t) argc; i > 2; --i) {
      s.len += strlen(argv[i]) + 1;
    }
    s.buf = malloc(s.len + (size_t) argc);
    if (s.buf) {
      size_t len;
      s.buf[s.len - 1] = '\0';
      for (len = 0, i = (size_t) argc; i > 2; --i) {
        strcpy(s.buf + len, argv[i]);
        len += strlen(argv[i]) + 1;
        s.buf[len - 1] = ' ';
      }
    }
  }
  return s;
}
#endif

/*** Map ***/

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
      m.buf = (char *) mmap(NULL, m.len, PROT_READ, MAP_SHARED, fd, 0);
    }
    close(fd);
  }
  return m;
}

/*** Important Functions ***/

static string_t
find_region(map_t m, string_t startsym, string_t stopsym) {
  extern char * strndup(const char * s, size_t n); /* for splint */
  char * buf = NULL, * start, * stop;
  size_t len;

  start = find(startsym, m.buf, m.buf + m.len);

  if (start) {
    start += startsym.len;

#ifdef REQUIRE_SPACE
    if (!isspace(*start)) {
      fprintf(stderr, RED "%s" RESET ": Found start without suffix spacing.\n", g_filename.buf);
      return (string_t) { 0 , buf };
    }
#endif /* REQUIRE_SPACE */

    stop = find(stopsym, start, start + m.len - (start - m.buf));

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
      len = (size_t) (stop - m.buf) - (start - m.buf);
      buf = strndup(start, len);
    }
  }
  return (string_t) { len, buf };
}

static string_t
file_find_region(char * fn, string_t start, string_t stop) {
  string_t s = { 0, NULL };
  map_t m = map(fn);
  if (m.buf) {
    s = find_region(m, start, stop);
    munmap(m.buf, m.len);
  }
  return s;
}

static int
root(string_t s) {
  char x[1] = {'\0'};
  char * root = s.buf;
  size_t len = s.len;
  int ret;

  while (len && root[len] != '/')
  { --len; }

  if (!len) { return 0; }

  swap(root + len, x);
  ret = chdir(root);
  swap(root + len, x);

/* *rootp += len + 1; */
  return ret;
}

#define ARRLEN(x) (sizeof(x) / sizeof(x[0]))

static string_t
expand(string_t s) {
  enum {
    MACRO_FILENAME = 0,
    MACRO_SHORT    = 1,
    MACRO_ARGS     = 2,
  };

  string_t macro[] = {
    [MACRO_FILENAME] = { 2, "$@" },
    [MACRO_SHORT   ] = { 2, "$*" },
    [MACRO_ARGS    ] = { 2, "$+" },
  };

  size_t i, f;

  {
    size_t max = s.len;
    for (i = 0; i < s.len; ++i) {
      for (f = 0; f < ARRLEN(macro); ++f) {
        if (!strncmp(s.buf + i, macro[f].buf, macro[f].len)) {
          max += globals[f].len;
        }
      }
    }
    s.buf = realloc(s.buf, max + 27); /* I don't know, man */
    if (!s.buf) { return (string_t) { 0, NULL}; }
    memset(s.buf + s.len, 0, max - s.len);
    s.len = max;
  }
  for (i = 0; i < s.len; ++i) {
    for (f = 0; f < ARRLEN(macro); ++f) {
      if (!strncmp(s.buf + i, macro[f].buf, macro[f].len)) {
        insert(s.buf + i, s.len - i, globals[f].buf, globals[f].len, 2);
      }
    }
  }
  return s;
}

/* Strips all prefixing and leading whitespace.
 * Except if the last character beforehand is a newline. */
static size_t
strip(string_t s) {
  size_t i = s.len;

  if (!i)
  { return 0; }

  while (isspace(s.buf[i]))
  { --i; }

  s.buf[i] = '\0';
  for (i = 0; isspace(s.buf[i]); ++i);

  return i - (s.buf[i - 1] == '\n');
}

static int
run(char * buf) {
  int ret = 127;
  fputs(GREEN "output" RESET ":\n", stderr);
  pid_t pid = fork();
  if (!pid) {
    execl("/bin/sh", "sh", "-c", buf, NULL);
  } else {
    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status)) { ret = 126; }
    else { ret = WEXITSTATUS(status); }
  }
  return ret;
}

int
main(int argc, char ** argv) {
  int ret = 0;
  string_t s = { 0, NULL };

  if (argc < 2
  ||  streq(argv[1], "-h")
  ||  streq(argv[1], "--help"))
  { goto help; }

  g_filename = (string_t) { strlen(argv[1]), argv[1] };

  if (streq(argv[1], "-n")
  ||  streq(argv[1], "--dry-run")) {
    if (argc > 2) {
      ret = 1;
      g_filename = (string_t) { strlen(argv[2]), argv[2] };
    }
    else { goto help; }
  }

  s = file_find_region(g_filename.buf, START, STOP);

  if (!s.buf) {
    if (errno)
    { fprintf(stderr, BOLD RED "%s" RESET ": %s\n", g_filename.buf, strerror(errno)); }
    else
    { fprintf(stderr, BOLD RED "%s" RESET ": File unrecognized.\n", g_filename.buf); }
    return 1;
  }

  g_all = all_args(argc, argv);
  g_short = shorten(g_filename);
  root(g_filename);

  s = expand(s);
  free(g_short.buf); free(g_all.buf);
  if (!s.buf) { return 1; }

  fprintf(stderr, GREEN "%s" RESET ": %s\n", argv[0], s.buf + strip(s));
  ret = ret ? 0 : run(s.buf);
  if (ret)
  { fprintf(stderr, RED "result" RESET ": " BOLD "%d\n" RESET, ret); }

  free(s.buf);
  return ret;
help:
  fprintf(stderr, YELLOW "%s" RESET ": %s", argv[0], HELP DESC);
  return 1;
}
 
