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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef SHAKE_COMPAT
# define HELP                                                           \
  "target-file [arguments ...]\n"                                       \
  "Use the format `EXEC:command ...:STOP' within the target-file\n"
# define START "EXEC:"
# define STOP ":STOP"
#else
# define HELP                                                           \
  "target-file [arguments ...]\n"                                       \
  "Use the format `@COMPILECMD command ...\n' within the target-file\n"
# define START "@COMPILECMD "
# define STOP "\n"
#endif

#define DESC                                                          \
  "\t$@  returns target-file\n"                                       \
  "\t$*  returns target-file without suffix\n"                        \
  "\t$+  returns arguments\n"

#define local_assert(expr, ret) do { assert(expr); if (!expr) { return ret; }} while (0)

static char * g_filename, * g_short, * g_all;

static char *
find(char * buf, const char * token)
{
  size_t len = strlen(token);
  const char * stop = buf + strlen(buf);
  do if (!strncmp(buf,token,len))
  { return buf + len; }
  while (buf < stop && ++buf);
  return NULL;
}

static char *
load(const char * fn)
{
  char * buf = NULL;
  FILE * fp = fopen(fn, "rb");
  if (fp)
  {
    struct stat s;
    off_t len;
    if (! stat(fn,&s)
    &&    s.st_mode & S_IFREG)
    {
      len = s.st_size;
      buf = malloc(len + 1);
      fread(buf, 1, len, fp);
      buf[len] = '\0';
    }
    fclose(fp);
  }
  return buf;
}

static char *
find_region(const char * fn)
{
  size_t len;
  char * buf, * start, * stop;

  if (!(buf = load(fn)))
  { if (errno) { fprintf(stderr, "cannot access '%s': ", fn); } return NULL; }

  if (!(start = find(buf,   START))
  ||  !(stop  = find(start, STOP)))
  { fprintf(stderr, "No usable format located in '%s'\n", fn); free(buf); return NULL; }

  len = stop - start - strlen(STOP);
  memmove(buf, start, len);
  buf[len] = '\0';

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
  max = strlen(str) + 1;
  memmove(str + offset + len, str + offset + shift, max - offset - shift);
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
expand_size(char * buf, size_t len, int argc, char ** argv)
{
  size_t i, max = len;
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
        max += strlen(g_short);
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
expand(char * buf, size_t len)
{
  size_t i;
  char * ptr = NULL;
  buf = realloc(buf, len);
  local_assert(buf, NULL);
  for (i = 0; i < len; ++i)
  {
    if (buf[i] == '\\')
    { i += 2; continue; }
    else if (buf[i] == '$')
    {
      switch (buf[++i])
      {
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
  { fprintf(stderr, "%s: %s", argv[0], HELP DESC); return 1; }

  g_filename = argv[1];

  if (!strcmp(argv[1], "-n"))
  { ret = 1; g_filename = argv[2]; }

  buf = find_region(g_filename);
  if (!buf)
  { if (errno) { perror(NULL); } return 1; }

  buf = expand(buf, expand_size(buf, strlen(buf), argc, argv) + 1);

  fprintf(stderr, "Exec: %s\n", buf + strip(buf) - (buf[0] == '\n'));
  if ((ret = ret ? 0 : run(buf)))
  { fprintf(stderr, "Result: %d\n", ret); }

  free(buf);
  return ret;
}
