/* EXEC:cc $@ -o $* -std=gnu89 -O2 -Wall -Wextra -Wpedantic -pipe $CFLAGS:STOP */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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
    size_t len;
    if (! stat(fn,&s)
    &&    s.st_mode & S_IFREG
    &&   (len = s.st_size)
    &&   (buf = malloc(len + 1))
    &&    fread(buf, 1, len, fp) > strlen(STOP) + strlen(START))
    { buf[len] = '\0'; }
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
  { fprintf(stderr, errno ? "cannot access '%s':" : "'%s': file too short", fn); return NULL; }

  if (!(start = find(buf,   START))
  ||  !(stop  = find(start, STOP)))
  { fprintf(stderr, "No usable format located in '%s'", fn); return NULL; }

  len = stop - start - strlen(STOP);
  memmove(buf, start, len);
  buf = realloc(buf, len + 1);
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
  char x[1] = "\0";
  int ret;
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
  size_t len = strlen(new);
  size_t max = strlen(str) + 1;
  /* str = realloc(str, max + len); */
  memmove(str + offset + len, str + offset + shift, max - offset - shift);
  memcpy(str + offset, new, len);
  return str;
}

static char *
expand(char * buf, char ** str)
{
  size_t i, len = strlen(buf);
  int x;
  buf = realloc(buf, 500);
  for (i = 0; i < len; ++i)
  {
    if (buf[i] == '\\')
    { i += 2; continue; }
    else if (buf[i] == '$')
    {
      switch (buf[++i])
      {
      case '@': x = 0; break;
      case '*': x = 1; break;
      case '+': x = 2; break;
      default: continue;
      }
      buf = insert(str[x], buf, i - 1, 2);
      len = strlen(buf);
    }
  }
  return buf;
}

static char *
shorten(char * fn)
{
  size_t i, last = 0, len = strlen(fn);
  char * sh = malloc(len + 1);
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

int
main(int argc, char ** argv)
{
  int ret;
  char * str[3];
  char * buf;
  if (argc < 2)
  {
    fprintf(stderr, "%s: %s", argv[0], HELP DESC);
    return 1;
  }

  /* filename */
  str[0] = argv[1];

  /* sh */
  str[1] = shorten(argv[1]);

  /* all */

  str[2] = all_args((size_t) argc, argv);

  buf = find_region(argv[1]);
  root(argv[1]);
  buf = expand(buf, str);
  fprintf(stderr, "Exec: %s\nOutput:\n", buf);
  fprintf(stderr, "Result: %d\n", (ret = system(buf)));
  free(str[2]);
  free(str[1]);
  free(buf);
  return ret;
}
