/* bake.c - Ever burned a cake?
 * Copyright 2023 Emil Williams
 *
 * Licensed under the GNU Public License version 3 only, see LICENSE.
 *
 * @BAKE cc -std=c89 -O2 @FILENAME -o @{@SHORT} @ARGS @STOP
 */

#define _GNU_SOURCE
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

#define VERSION "20240413"

#define  HELP                                                                                         \
  BOLD "[option] target-file" RESET " [" GREEN "arguments" RESET " ...]\n"                            \
  "Use the format `" BOLD "@BAKE" RESET " cmd ...' within the target-file, this will execute the\n"   \
  "rest of line, or if found within the file, until the " BOLD "@STOP" RESET " marker.\n"

#define DESC                                                                            \
  "Options [Must always be put first, may be merged together]\n"                        \
  "\t" DIM "-v --version" RESET ", " DIM "-h --help" RESET ", "                         \
  BOLD "-n --dry-run" RESET ", " BOLD "-x --expunge" RESET ", "                         \
  BOLD "-c --color\n" RESET                                                             \
  "Expansions\n"                                                                        \
  "\t" YELLOW "@FILENAME" RESET "  returns target-file                (abc.x.txt)\n"    \
  "\t" YELLOW "@SHORT   " RESET "  returns target-file without suffix (^-> abc.x)\n"    \
  "\t" YELLOW "@ARGS    " RESET "  returns " GREEN "arguments" RESET "\n"               \
  "Additional Features And Notes\n"                                                     \
  "\t" YELLOW "@{" RESET BOLD "EXPUNGE_THIS_FILE" YELLOW "}" RESET                      \
  " inline region to delete this or many files or directories,\n"                       \
  "\tnon-recursive, only one file per block, removed from left to right. This has no\n" \
  "\tinfluence on the normal command execution.\n"                                      \
  "\t" YELLOW "\\" RESET                                                                \
  "SPECIAL_NAME will result in SPECIAL_NAME in the executed shell command.\n"           \
  "Backslashing is applicable to all meaningful symbols in Bake, it is ignored otherwise."

#define COPYRIGHT "2023 Emil Williams"
#define LICENSE "Licensed under the GNU Public License version 3 only, see LICENSE."

#define FILENAME_LIMIT (FILENAME_MAX)

#define BAKE_ERROR 127

enum {
  BAKE_UNRECOGNIZED,
  BAKE_MISSING_SUFFIX
};

enum {
  BAKE_RUN     =       0,
  BAKE_NORUN   = (1 << 0),
  BAKE_EXPUNGE = (1 << 1)
};


#define ARRLEN(a) (sizeof(a) / sizeof(a[0]))

#if INCLUDE_AUTONOMOUS_COMPILE
  __attribute__((__section__(".text"))) static char autonomous_compile[] =
  "@BAKE cc -std=c89 -O2 $@.c -o $@ $+ @STOP";
#endif

static int bake_errno;

typedef struct {
  size_t len;
  char * buf;
} string_t;

typedef string_t map_t;

/*** nocolor printf ***/

#if ENABLE_COLOR
# define color_printf(...) color_fprintf(stdout, __VA_ARGS__)
/* not perfect, too simple, doesn't work with a var, only a literal. */
# define color_puts(msg) color_fprintf(stdout, msg "\n")

int color = ENABLE_COLOR;

static char * expand(char * buf, char * macro, char * with);

color_fprintf(FILE * fp, char * format, ...) {
  va_list ap;
  char * buf;
  va_start(ap, format);
  if (!color) {

    vasprintf(&buf, format, ap);

    if (buf) {
      expand(buf, RED, "");
      expand(buf, GREEN, "");
      expand(buf, YELLOW, "");
      expand(buf, DIM, "");
      expand(buf, BOLD, "");
      expand(buf, RESET, "");

      fwrite(buf, strlen(buf), 1, fp);
    }

    free(buf);
  } else {
    vfprintf(fp, format, ap);
  }
  va_end(ap);

}
#else
# define color_printf(...) fprintf(stdout, __VA_ARGS__)
# define color_puts(msg) puts(msg)
#endif

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

  while (len && root[len] != '/') {
    --len;
  }

  if (!len) {
    return 0;
  }

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
  map_t m;
  m.buf = NULL;
  m.len = 0;
  fd = open(fn, O_RDONLY);

  if (fd != -1) {
    if (!fstat(fd, &s)
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

  for (; buf <= end - xlen; ++buf) {
    if (!strncmp(buf, x, xlen)) {
      if (start < buf && buf[-1] == '\\') {
        continue;
      } else {
        return buf;
      }
    }
  }

  return NULL;
}

static char *
get_region(string_t m, char * findstart, char * findstop) {
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
            &&  stop[1] == '\n') {
          stop += 2;
        }

        ++stop;
      }
    }

    buf = strndup(start, (size_t)(stop - start));
  }

  return buf;
}

static char *
file_get_region(char * fn, char * start, char * stop) {
  map_t m = map(fn);
  char * buf = NULL;

  if (m.buf) {
    buf = get_region(m, start, stop);
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
    if (fn[i] == '.') {
      last = i;
    }
  }

  last = last ? last : i;
  strncpy(sh, fn, last);
  sh[last] = '\0';
  return sh;
}

static char *
all_args(size_t argc, char ** argv) {
  char * all = NULL;

  if (argc > 0) {
    size_t i, len = argc;

    for (i = 0; i < argc; ++i) {
      len += strlen(argv[i]);
    }

    all = malloc(len + 1);
    all[len] = '\0';

    for (len = 0, i = 0; i < argc; ++i) {
      strcpy(all + len, argv[i]);
      len += strlen(argv[i]) + 1;

      if (i + 1 < argc) {
        all[len - 1] = ' ';
      }
    }
  }

  return all ? all : calloc(1, 1);
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
          buf = realloc(buf, blen + nlen);
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
       * macro_old[MACRO_STOP],
       * global[MACRO_NONE];

  size_t i;

  macro[MACRO_FILENAME] = "@FILENAME";
  macro[MACRO_SHORT   ] =    "@SHORT";
  macro[MACRO_ARGS    ] =     "@ARGS";
  macro[MACRO_STOP    ] =     "@STOP";

  macro_old[MACRO_FILENAME] = "$@";
  macro_old[MACRO_SHORT   ] = "$*";
  macro_old[MACRO_ARGS    ] = "$+";

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

static void
remove_expand(char * buf, char * argv0, int rm, char * start, char * stop) {
  size_t i, f, end = strlen(buf);
  size_t startlen = strlen(start), stoplen = strlen(stop);
  char x[1] = {'\0'};


  for (i = 0; i < end; ++i) {
    if (!strncmp(buf + i, start, startlen)) {
      if (buf + i > buf && buf[i - 1] == '\\') {
        continue;
      }

      for (f = i; f < end; ++f) {
        if (!strncmp(buf + f, stop, stoplen)) {
          if (buf + f > buf && buf[f - 1] == '\\') {
            continue;
          }

          insert(buf + f, "", end - f, 0, 1);
          i += startlen;

          if (rm & BAKE_EXPUNGE) {
            swap(buf + i + (f - i), x);
#if !ENABLE_EXPUNGE_REMOVE
            color_printf("%s: %sremoving '%s'\n",
                         argv0, rm & BAKE_NORUN ? "not " : "", buf + i);

            if (!(rm & BAKE_NORUN)) {
              remove(buf + i);
            }

#else
            color_printf("%s: not removing '%s'\n", argv0, buf + i);
#endif
            swap(buf + i + (f - i), x);
          }

          goto next;
        }
      }

      goto stop;
    }

  next:;
  }

stop:
  expand(buf, start, "");
}

/*** strip, run ***/

/* Strips all prefixing and leading whitespace.
 * Except if the last character beforehand is a newline. */
static size_t
strip(char * buf) {
  size_t i = strlen(buf);

  if (!i) {
    return 0;
  }

  while (i && isspace(buf[i - 1])) {
    --i;
  }

  buf[i] = '\0';

  for (i = 0; isspace(buf[i]); ++i);

  if (i && buf[i - 1] == '\n') {
    --i;
  }

  return i;
}

static int
run(char * buf, char * argv0) {
  pid_t pid;
  color_puts(BOLD GREEN "output" RESET ":\n");

  if ((pid = fork()) == 0) {
    execl("/bin/sh", "sh", "-c", buf, NULL);
    return 0; /* execl overwrites the process anyways */
  } else if (pid == -1) {
    color_fprintf(stderr, BOLD RED "%s" RESET ": %s, %s\n",
                  argv0, "Fork Error", strerror(errno));
    return BAKE_ERROR;
  } else {
    int status;

    if (waitpid(pid, &status, 0) < 0) {
      color_fprintf(stderr, BOLD RED "%s" RESET ": %s, %s\n",
                    argv0, "Wait PID Error", strerror(errno));
      return BAKE_ERROR;
    }

    if (!WIFEXITED(status)) {
      return BAKE_ERROR;
    }

    return WEXITSTATUS(status);
  }
}

/*** main ***/

int
main(int argc, char ** argv) {
  int ret = BAKE_RUN;
  char * buf = NULL,
         * filename,
         * argv0;

  argv0 = argv[0];

  if (argc < 2) {
    goto help;
  }

  while (++argv, --argc && argv[0][0] == '-') {
    ++argv[0];

    if (argv[0][1] == '-') {
      ++argv[0];

      if (!strcmp(argv[0],    "help")) {
        goto help;
      } else if (!strcmp(argv[0], "version")) {
        goto version;
      } else if (!strcmp(argv[0], "expunge")) {
        ret |= BAKE_EXPUNGE;
      } else if (!strcmp(argv[0], "dry-run")) {
        ret |= BAKE_NORUN;
      } else if (!strcmp(argv[0],   "color")) {
#if ENABLE_COLOR
        color = 0;
#endif
      } else                                  {
        goto help;
      }
    } else do {
        switch (argv[0][0]) {
        case 'h':
          goto help;

        case 'v':
          goto version;

        case 'x':
          ret |= BAKE_EXPUNGE;
          break;

        case 'n':
          ret |= BAKE_NORUN;
          break;

#if ENABLE_COLOR

        case 'c':
          color = 0;
          break;
#endif

        case 0  :
          goto next;

        default :
          color_puts("UNKNOWN");
          goto help;
        }
      } while (++(argv[0]));

  next:;
  }

  if (!argc)
  {
    color_fprintf(stderr, BOLD RED "%s" RESET
                  ": No filename provided\n",
                  argv0);
    return BAKE_ERROR;
  }

  filename = argv[0];
  ++argv, --argc;

  if (strlen(filename) > FILENAME_LIMIT) {
    color_fprintf(stderr, BOLD RED "%s" RESET
                  ": Filename too long (exceeds %d)\n",
                  argv0,
                  FILENAME_LIMIT);
    return BAKE_ERROR;
  }

  root(&filename);
  buf = file_get_region(filename, START, STOP);

  if (!buf) {
    char * error[2];
    error[0] = "File Unrecognized";
    error[1] = "Found start without suffix spacing";

    color_fprintf(stderr, BOLD RED "%s" RESET ": '" BOLD "%s" RESET "' %s.\n",
                  argv0, filename, errno ? strerror(errno) : error[bake_errno]);
    return BAKE_ERROR;
  }

  buf = bake_expand(buf, filename, argc, argv);

  color_printf(BOLD GREEN "%s" RESET ": %s\n", argv0, buf + strip(buf));

  remove_expand(buf, argv0, ret, EXPUNGE_START, EXPUNGE_STOP);

  if (!ret) {
    ret = run(buf, argv0);

    if (ret) {
      color_printf(BOLD RED "result" RESET ": " BOLD "%d\n" RESET, ret);
    }
  } else { ret = 0; }

  free(buf);
  return ret;
help:
  color_fprintf(stderr, YELLOW "%s" RESET ": %s\n", argv0, HELP DESC);
  return BAKE_ERROR;
version:
  color_fprintf(stderr,
                YELLOW "%s" RESET ": v" VERSION "\n"
                "Copyright " COPYRIGHT "\n" LICENSE "\n",
                argv0);
  return BAKE_ERROR;
}
