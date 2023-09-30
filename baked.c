/* baked.c - Ever burned a cake?
 * Copyright 2023 Emil Williams
 *
 * Licensed under the GNU Public License version 3 only, see LICENSE.
 *
 * EXEC:cc $@ -o $* -std=gnu89 -O2 -Wall -Wextra -Wpedantic -pipe $CFLAGS:STOP
 * @COMPILECMD cc $@ -o $* -std=gnu89 -O2 -Wall -Wextra -Wpedantic -pipe $CFLAGS
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef SHAKE_COMPAT
# define HELP                                                           \
  "target-file [arguments ...]\n"                                       \
  "Use the format `EXEC:command ...:STOP' within the target-file\n"
# define START "EXEC:"
# define STOP ":STOP"
# define SLEN 5
#else
# define HELP                                                           \
  "target-file [arguments ...]\n"                                       \
  "Use the format `@COMPILECMD command ...\n' within the target-file\n"
# define START "@COMPILECMD "
# define STOP "\n"
# define SLEN 12
#endif

#define DESC                                                          \
  "Options [Must always be first]\n"                                  \
  "\t-h, this message, -n dryrun\n"                                   \
  "In-file expansions\n"                                              \
  "\t$@  returns target-file\n"                                       \
  "\t$*  returns target-file without suffix\n"                        \
  "\t$+  returns arguments\n"

#define local_assert(expr, ret) do { assert(expr); if (!expr) { return ret; }} while (0)

static char * g_filename, * g_short, * g_all;

static char *
find_region(const char * fn)
{
  struct stat s;
  int fd;
  char * buf = NULL;

  fd = open(fn, O_RDONLY);

  if ( fd != -1
  &&  !fstat(fd,&s)
  &&   s.st_mode & S_IFREG
  &&   s.st_size)
  {
    char * start, * stop, * addr;
    /* hypothetically mmap can return -1 (MAP_FAILED) on failure,
       this will not be due to permission, noexist, or ifreg.
       Checks ensures that all these possibilities are impossible.
       However, other issues may occur, such as a unexpected
       change to permission after the fstat call had succeeded.
       Then, this call may fail, Under such condition the for loop
       will safely lead to a bail with a possible bad call of munmap. */
    addr = mmap(NULL, s.st_size, PROT_READ, MAP_SHARED, fd, 0);

    for (start = addr; *start; ++start)
    {
      if (s.st_size - (start - addr) > SLEN)
      {
        if (!strncmp(start,START,SLEN))
        {
          start += strlen(START);
          for (stop = start; *stop; ++stop)
          {
            if (s.st_size - (stop - addr) > SLEN)
            {
              if (!strncmp(stop,STOP,SLEN))
              {
                size_t len = (stop - addr) - (start - addr);
                buf = malloc(len + 1);
                assert(buf);
                if (!buf)
                { goto stop; }
                strncpy(buf, start, len);
                buf[len] = '\0';
                goto stop;
              }
            }
            else goto stop;
          }
          goto stop;
        }
      }
      else goto stop;
    }
  stop:
    if (addr != MAP_FAILED)
    { munmap(addr, s.st_size); }
  }
  return buf;
}

static void
swap(char * a, char * b)
{
  *a ^= *b;
  *b ^= *a;
  *a ^= *b;
}

static int
root(char * root)
{
  int ret;
  char x[1] = "\0";
  size_t len = strlen(root);
  while (len && root[len] != '/')
  { --len; }
  if (!len)
  { return 0; }
  swap(root + len, x);
  ret = chdir(root);
  swap(root + len, x);
  return ret;
}

static char *
insert(const char * new, char * str, size_t offset, size_t shift)
{
  size_t len, max;
  local_assert(new, str);
  local_assert(str, NULL);
  len = strlen(new);
  max = (strlen(str) + 1 - offset - shift);
  memmove(str + offset + len, str + offset + shift, max);
  memcpy(str + offset, new, len);
  return str;
}

static char *
shorten(char * fn)
{
  size_t i, last = 0, len;
  char * sh;
  local_assert(fn, NULL);
  len = strlen(fn);
  sh = malloc(len + 1);
  local_assert(sh, NULL);
  for (i = 0; i < len; ++i)
  {
    if (fn[i] == '.')
    { last = i; }
  }
  last = last ? last : i;
  strncpy(sh, fn, last);
  sh[last] = '\0';
  return sh;
}

static char *
all_args(size_t argc, char ** argv)
{
  char * all = NULL;
  if (argc > 2)
  {
    size_t i, len = 0;
    for (i = 2; i < argc; ++i)
    { len += strlen(argv[i]); }
    all = malloc(len + 1);
    local_assert(all, NULL);
    all[len] = '\0';
    len = 0;
    for (i = 2; i < argc; ++i)
    {
      strcpy(all + len, argv[i]);
      len += strlen(argv[i]);
    }
  }
  return all;
}

static size_t
expand_size(char * buf, int argc, char ** argv)
{
  size_t i, len, max;
  len = max = strlen(buf) + 1;
  for (i = 0; i < len; ++i)
  {
    if (buf[i] == '\\')
    { i += 2; continue; }
    else if (buf[i] == '$')
    {
      switch (buf[++i])
      {
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
expand(char * buf)
{
  size_t i;
  char * ptr = NULL;
  for (i = 0; buf[i]; ++i)
  {
    if (buf[i] == '\\')
    { i += 2; continue; }
    else if (buf[i] == '$')
    {
      switch (buf[++i])
      {
      case '@':
        /* against the advice of -fanalyzer
         *
         * -- Supposed use of NULL is IMPOSSIBLE by standards definition:
         *
         * If the value of argc is greater than zero, the array members
         * argv[0] through argv[argc-1] inclusive shall contain pointers
         * to strings [...]
         *
         * -- Under the codition that argc is either not <2 and under
         * the correct context, >2, the prorgam will not continue under
         * either of these failure states and hence this is without
         * doubt an impossibility.
         * Unless an unaccounted for UB or a manual control flow error
         * exists, for which may interfere with these conditions, this
         * must certainly be a false-positive.
         */
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

static size_t
strip(char * buf)
{
  size_t i = strlen(buf);
  while (isspace(buf[i - 1]))
  { --i; }
  buf[i] = '\0';
  for (i = 0; isspace(buf[i]); ++i);
  return i;
}

static int
run(const char * buf)
{
  fputs("Output:\n", stderr);
  root(g_filename);
  return system(buf);
}

int
main(int argc, char ** argv)
{
  int ret = 0;
  char * buf;

  if (argc < 2
  ||  !strcmp(argv[1], "-h"))
  { goto help; }

  g_filename = argv[1];

  if (!strcmp(argv[1], "-n"))
  {
    if (argc > 2)
    { ret = 1; g_filename = argv[2]; }
    else
    { goto help; }
  }

  buf = find_region(g_filename);
  if (!buf)
  {
    if (errno)
    { perror(argv[0]); }
    else
    { fprintf(stderr, "%s: File unrecognized.\n", argv[0]); }
    return 1;
  }

  buf = realloc(buf, expand_size(buf, argc, argv));
  local_assert(buf, 1);
  buf = expand(buf);
  fprintf(stderr, "Exec: %s\n", buf + strip(buf) - (buf[0] == '\n'));
  if ((ret = ret ? 0 : run(buf)))
  { fprintf(stderr, "Result: %d\n", ret); }

  free(buf);
  return ret;
help:
  fprintf(stderr, "%s: %s", argv[0], HELP DESC);
  return 1;
}
