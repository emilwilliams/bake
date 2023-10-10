/* baked-nobloat.c - cake makes you fat. This version just removes features I don't find entirely necessary ($+, require space, shake support, ctype, and options).
 * Copyright 2023 Emil Williams
 * Licensed under the GNU Public License version 3 only, see LICENSE.
 * @EXEC cc $@ -o $* -std=gnu89 -O2 -Wall -Wextra -Wpedantic -pipe $CFLAGS STOP@
 */

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define START "@EXEC"
#define  STOP "STOP@"
#define  HELP                                                                          \
    "target-file [arguments ...]\n"                                                    \
    "Use the format `@EXEC cmd ...' within the target-file, this will execute the\n"   \
    "rest of line, or if found within the file, until the STOP@ marker.\n"

#define DESC                                                \
  "Expansions\n"                                            \
  "\t$@  returns target-file                (abc.x.txt)\n"  \
  "\t$*  returns target-file without suffix (^-> abc.x)\n"

char * g_filename, * g_short;

static char *
map(const char * fn, size_t * len)
{
  struct stat s;
  int fd;
  char * addr = NULL;
  fd = open(fn, O_RDONLY);
  if (fd != -1)
  {
    if (!fstat(fd,&s)
        &&   s.st_mode & S_IFREG
        &&   s.st_size)
    {
      *len = s.st_size;
      addr = mmap(NULL, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
      if (addr == MAP_FAILED)
      { addr = NULL; }
    }
    close(fd);
  }
  return addr;
}

static const char *
find(const char * x, const char * buf, const size_t max, const size_t min)
{
  const char * start = buf;
  for (; *buf; ++buf)
  {
    if (max - (buf - start) > min && !strncmp(buf, x, min))
    { return buf; }
  }
  return NULL;
}

static char *
find_region(const char * fn, const char * start, const char * stop)
{
  size_t len = 0;
  char * buf = NULL, * addr;
  const char * pb, * pe;
  addr = map(fn, &len);
  if (addr != MAP_FAILED)
  {
    if ((pb = find(start, addr, len, strlen(start))))
    {
      pb += strlen(start);
      pe = find(stop, pb, len - (pb - addr), strlen(stop));
      if (!pe)
      {
        pe = pb;
        while (*pe && *pe != '\n')
        {
          if (pe[0] == '\\' && pe[1] == '\n')
          { pe += 2; }
          ++pe;
        }
      }
      if (pe)
      { buf = strndup(pb, (pe - addr) - (pb - addr)); }
    }
    munmap(addr, len);
  }
  return buf;
}


static char *
insert(const char * new, char * str, size_t offset, size_t shift)
{
  size_t len, max;
  if (!new || !str) { return NULL; }
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
  len = strlen(fn);
  sh = malloc(len + 1);
  if (!sh) { return NULL; }
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

static size_t
expand_size(char * buf)
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
  buf = realloc(buf, expand_size(buf));
  if (!buf) { return NULL; }
  for (i = 0; buf[i]; ++i)
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
      default: continue;
      }
      buf = insert(ptr, buf, i - 1, 2);
    }
  }
  free(g_short);
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

int
main(int argc, char ** argv)
{
  char * buf;
  setlocale(LC_ALL, "C");
  if (argc < 2) { goto help; }
  g_filename = argv[1];
  buf = find_region(g_filename, START, STOP);
  if (!buf)
  {
    if (errno)
    { perror(argv[0]); }
    else
    { fprintf(stderr, "%s: File unrecognized.\n", argv[0]); }
    return 1;
  }
  buf = expand(buf);
  root(argv[0]);
  fprintf(stderr, "Exec: %s\n", buf);
  if (!buf) { return 1; }
  fputs("Output:\n", stderr);
  fflush(stderr);
  system(buf);
  free(buf);
  return 0;
help:
  fprintf(stderr, "%s: %s", argv[0], HELP DESC);
  return 1;
}
