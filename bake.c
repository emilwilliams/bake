/* bake.c - Ever burned a cake?
 * Copyright 2023 Emil Williams
 *
 * Licensed under the GNU Public License version 3 only, see LICENSE.
 *
 * @BAKE cc -std=c89 -O2 @FILENAME -o @SHORT @ARGS @STOP
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
#include <limits.h>

#include "config.h"

#define START "@BAKE"
#define  STOP "@STOP"

#define  HELP                                                                                         \
  BOLD "[option] target-file" RESET " [" GREEN "arguments" RESET " ...]\n"                            \
  "Use the format `" BOLD "@BAKE" RESET " cmd ...' within the target-file, this will execute the\n"   \
  "rest of line, or if found within the file, until the " BOLD "@STOP" RESET " marker.\n"             \

#define DESC                                                 \
  "Options [Must always be first]\n"                         \
  "\t" DIM "-h --help" RESET", " BOLD "-n --dry-run\n" RESET \
  "Expansions\n"                                             \
  "\t" YELLOW "@FILENAME" RESET "  returns target-file                (abc.x.txt)\n"  \
  "\t" YELLOW "@SHORT   " RESET "  returns target-file without suffix (^-> abc.x)\n"  \
  "\t" YELLOW "@ARGS    " RESET "  returns " GREEN "arguments" RESET "\n"

#define FILENAME_LIMIT (FILENAME_MAX)

#define BAKE_ERROR 127

enum {
  BAKE_UNRECOGNIZED,
  BAKE_MISSING_SUFFIX,
};

#define ARRLEN(a) (sizeof(a) / sizeof(a[0]))

#if INCLUDE_AUTONOMOUS_COMPILE
__attribute__((__section__(".text"))) static char autonomous_compile[] = "@BAKE cc -std=c89 -O2 $@.c -o $@ $+ @STOP";
#endif

static int bake_errno;

typedef struct {
  char * buf;
  size_t len;
} map_t;

/*** root ***/

static void
swap(char * a, char * b) {
  *a ^= *b;
  *b ^= *a;
  *a ^= *b;
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

/*** find region in file ***/

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

static char *
find(char * buf, char * x, char * end) {
  size_t xlen = strlen(x);
  char * start = buf;
  for (; buf < end - xlen; ++buf) {
    if (!strncmp(buf, x, xlen)) {
      if (start < buf && buf[-1] == '\\') { continue; }
      else { return buf; }
    }
  }
  return NULL;
}

static char *
find_region(map_t m, char * findstart, char * findstop) {
  char * buf = NULL, * start, * stop, * end = m.len + m.buf;
  start = find(m.buf, findstart, end);

  if (start) {
    start += strlen(findstart);

#ifdef REQUIRE_SPACE
    if (!isspace(*start)) {
      bake_errno = BAKE_MISSING_SUFFIX;
      return NULL;
    }
#endif /* REQUIRE_SPACE */

    stop = find(start, findstop, end);

    if (!stop) {
      stop = start;
      while (stop < end && *stop != '\n') {
        if (stop[0] == '\\'
        &&  stop[1] == '\n') { stop += 2; }
        ++stop;
      }
    }

    if (stop)
    { buf = strndup(start, (size_t) (stop - m.buf) - (start - m.buf)); }
  }
  return buf;
}

static char *
file_find_region(char * fn, char * start, char * stop) {
  char * buf = NULL;
  map_t m = map(fn);
  if (m.buf) {
    buf = find_region(m, start, stop);
    munmap(m.buf, m.len);
  }
  return buf;
}

/*** g_short, g_all ***/

static char *
shorten(char * fn) {
  size_t i, last, len;
  static char sh[FILENAME_LIMIT];
  len = strlen(fn);
  for (last = i = 0; i < len; ++i) {
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
    all[len] = '\0';
    for (len = 0, i = 2; i < argc; ++i) {
      strcpy(all + len, argv[i]);
      len += strlen(argv[i]) + 1;
      if (i + 1 < argc) { all[len - 1] = ' '; }
    }
  }
  return all ? all : calloc(1,1);
}

/*** insert, expand, bake_expand ***/

static void
insert(char * str, char * new, size_t slen, size_t nlen, size_t shift) {
  memmove(str + nlen, str + shift, slen + 1 - shift);
  memcpy(str, new, nlen);
}

static char *
expand(char * buf, char * macro, char * with) {
  ssize_t i,
         blen = strlen(buf),
         mlen = strlen(macro),
         wlen = strlen(with),
         nlen;
  fflush(stdout);
  for (i = 0; i < blen - mlen + 1; ++i) {
    if (!strncmp(buf + i, macro, mlen)) {
      if (i && buf[i - 1] == '\\') {
        memmove(buf + i - 1, buf + i, blen - i);
        buf[blen - 1] = '\0';
      } else {
        nlen = wlen - mlen + 1;
        if (nlen > 0) {
          buf   = realloc(buf, blen + nlen);
        }
        insert(buf + i, with, blen - i, wlen, mlen);
        blen += (nlen > 0) * nlen;
      }
    }
  }
  return buf;
}

static char *
bake_expand(char * buf, char * filename, int argc, char ** argv) {
  enum {
    MACRO_FILENAME,
    MACRO_SHORT,
    MACRO_ARGS,
    MACRO_STOP,
    MACRO_NONE
  };

  char * macro[MACRO_NONE],
       * macro_old[MACRO_STOP];

  size_t i;

  macro[MACRO_FILENAME] = "@FILENAME";
  macro[MACRO_SHORT   ] =    "@SHORT";
  macro[MACRO_ARGS    ] =     "@ARGS";
  macro[MACRO_STOP    ] =     "@STOP";

  macro_old[MACRO_FILENAME] = "$@";
  macro_old[MACRO_SHORT   ] = "$*";
  macro_old[MACRO_ARGS    ] = "$+";

  char * global[4];

  global[MACRO_FILENAME] = filename;
  global[MACRO_SHORT   ] = shorten(filename);
  global[MACRO_ARGS    ] = all_args((size_t) argc, argv);
  global[MACRO_STOP    ] = "";

#if NEW_MACROS
  for (i = 0; i < ARRLEN(macro); ++i) {
    buf = expand(buf, macro[i], global[i]);
  }
#endif

#if OLD_MACROS
  for (i = 0; i < ARRLEN(macro_old); ++i) {
    buf = expand(buf, macro_old[i], global[i]);
  }
#endif

  free(global[MACRO_ARGS]);

  return buf;
}

/*** strip, run ***/

/* Strips all prefixing and leading whitespace.
 * Except if the last character beforehand is a newline. */
static size_t
strip(char * buf) {
  size_t i = strlen(buf);
  if (!i) { return 0; }
  while (i && isspace(buf[i - 1])) { --i; }
  buf[i] = '\0';
  for (i = 0; isspace(buf[i]); ++i);
  if (i && buf[i - 1] == '\n') { --i; }
  return i;
}

static int
run(char * buf, char * argv0) {
  fputs(BOLD GREEN "output" RESET ":\n", stderr);
  pid_t pid;
  if ((pid = fork()) == 0) {
    execl("/bin/sh", "sh", "-c", buf, NULL);
    return 0; /* execl overwrites the process anyways */
  } else if (pid == -1) {
    fprintf(stderr, BOLD RED "%s" RESET ": %s, %s\n", argv0, "Fork Error", strerror(errno));
    return BAKE_ERROR;
  } else {
    int status;
    if (waitpid(pid, &status, 0) < 0) {
      fprintf(stderr, BOLD RED "%s" RESET ": %s, %s\n", argv0, "Wait PID Error", strerror(errno));
      return BAKE_ERROR;
    }
    if (!WIFEXITED(status)) { return BAKE_ERROR; }
    return WEXITSTATUS(status);
  }
}

/*** main ***/

int
main(int argc, char ** argv) {
  int ret = 0;
  char * buf = NULL,
       * filename,
       * argv0;

  argv0 = argv[0];

  if (argc < 2
  ||  !strcmp(argv[1], "-h")
  ||  !strcmp(argv[1], "--help"))
  { goto help; }

  if (!strcmp(argv[1], "-n")
  ||  !strcmp(argv[1], "--dry-run")) {
    if (argc > 2) {
      ret = 1;
      --argc;
      ++argv;
    }
    else { goto help; }
  }

  filename = argv[1];
  if (strlen(filename) > FILENAME_LIMIT) {
    fprintf(stderr, BOLD RED "%s" RESET ": Filename too long (exceeds %d)\n", argv0, FILENAME_LIMIT);
    return BAKE_ERROR;
  }

  root(&filename);
  buf = file_find_region(filename, START, STOP);

  char * error[2];
  error[0] = "File Unrecognized";
  error[1] = "Found start without suffix spacing";

  if (!buf) {
    printf("%d\n", bake_errno);
    fprintf(stderr, BOLD RED "%s" RESET ": '" BOLD "%s" RESET "' %s.\n",
                    argv0, filename, errno ? strerror(errno) : error[bake_errno]);
    return BAKE_ERROR;
  }

  buf = bake_expand(buf, filename, argc, argv);

  fprintf(stderr, BOLD GREEN "%s" RESET ": %s\n", argv0, buf + strip(buf));
  ret = ret ? 0 : run(buf,argv0);
  if (ret)
  { fprintf(stderr, BOLD RED "result" RESET ": " BOLD "%d\n" RESET, ret); }

  free(buf);
  return ret;
help:
  fprintf(stderr, YELLOW "%s" RESET ": %s", argv0, HELP DESC);
  return BAKE_ERROR;
}
