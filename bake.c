/* bake.c - Ever burned a cake?
 * Copyright 2023 Emil Williams
 *
 * Licensed under the GNU Public License version 3 only, see LICENSE.
 *
 * @BAKE cc -std=c89 -O2 $@ -o $* $+ # @STOP
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

/* Require space after START */
#define REQUIRE_SPACE
/* Enable colors */
#define COLOR

#ifdef COLOR
#define    RED "\e[91m"
#define  GREEN "\e[92m"
#define YELLOW "\e[93m"
#define    DIM "\e[2m"
#define   BOLD "\e[1m"
#define  RESET "\e[0m"
#else
#define    RED
#define  GREEN
#define YELLOW
#define    DIM
#define   BOLD
#define  RESET
#endif

#define START "@BAKE"
#define  STOP "@STOP"
#define  HELP                                                                          \
    BOLD "target-file" RESET " [arguments ...]\n"                                      \
    "Use the format `" BOLD "@BAKE" RESET " cmd ...' within the target-file, this will execute the\n"   \
    "rest of line, or if found within the file, until the " BOLD "@STOP" RESET " marker.\n"             \

#define DESC                                                 \
  "Options [Must always be first]\n"                         \
  "\t" DIM "-h --help" RESET", " BOLD "-n --dry-run\n" RESET \
  "Expansions\n"                                             \
  "\t" YELLOW "$@" RESET "  returns target-file                (abc.x.txt)\n"  \
  "\t" YELLOW "$*" RESET "  returns target-file without suffix (^-> abc.x)\n"  \
  "\t" YELLOW "$+" RESET "  returns arguments\n"

/*** Utility functions ***/

static void
swap(char * a, char * b) {
  *a ^= *b;
  *b ^= *a;
  *a ^= *b;
}

static char *
find(char * x, char * buf, char * end) {
  size_t len = strlen(x);
  for (; (buf < end) && len < (size_t)(end - buf); ++buf) {
    if (!strncmp(buf, x, len))
    { return buf; }
  }
  return NULL;
}

static char *
insert(char * new, char * str, size_t offset, size_t shift) {
  size_t len, max;
  len = strlen(new);
  max = (strlen(str) + 1 - offset - shift);
  memmove(str + offset + len, str + offset + shift, max);
  memcpy(str + offset, new, len);
  return str;
}

/*** g_short, g_all Functions ***/

static char * g_filename, * g_short, * g_all;

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
  return sh;
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
  return all;
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
find_region(map_t m) {
  extern char * strndup(const char * s, size_t n); /* for splint */
  char * buf = NULL, * start, * stop;

  start = find(START, m.str, m.str + m.len);

  if (start) {
    start += strlen(START);

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
    { buf = strndup(start, (size_t) (stop - m.str) - (start - m.str)); }
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

static size_t
expand_size(char * buf, int argc, char ** argv) {
  size_t i, len, max;

  len = max = strlen(buf) + 1;

  for (i = 0; i < len; ++i) {
    if (buf[i] == '\\') {
      i += 2;
      continue;
    } else if (buf[i] == '$') {
      switch (buf[++i]) {
      case '@':
        max += strlen(g_filename);
        break;
      case '*':
        if (!g_short)
        { g_short = shorten(g_filename); }
        max += g_short ? strlen(g_short) : 0;
        break;
      case '+':
        if (!g_all)
        { g_all = all_args((size_t) argc, argv); }
        max += g_all ? strlen(g_all) : 0;
        break;
      }
    }
  }
  return max;
}

static char *
expand(char * buf) {
  size_t i;
  char * ptr = NULL;

  for (i = 0; buf[i]; ++i) {
    if (buf[i] == '\\') {
      i += 2;
      continue;
    } else if (buf[i] == '$') {
      switch (buf[++i]) {
      case '@':
        ptr = g_filename;
        break;
      case '*':
        ptr = g_short;
        break;
      case '+':
        ptr = g_all ? g_all : "";
        break;
      default: continue;
      }
      buf = insert(ptr, buf, i - 1, 2);
    }
  }
  free(g_short); free(g_all);
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

  /* changing this to "" creates many additional allocations */
  (void) setlocale(LC_ALL, "C");

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
      buf = find_region(m);
      munmap(m.str, m.len);
    }
  }

  if (!buf) {
    if (errno) { fprintf(stderr, BOLD RED "%s" RESET ": %s\n", g_filename, strerror(errno)); }
    else { fprintf(stderr, BOLD RED "%s" RESET ": File unrecognized.\n", g_filename); }
    return 1;
  }

  root(&g_filename);
  { char * buf2 = buf;
    buf = realloc(buf, expand_size(buf, argc, argv));
    if (!buf)
    { free(buf2); free(g_short); free(g_all); return 1; }
  }
  buf = expand(buf);

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
