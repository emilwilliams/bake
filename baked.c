/* baked.c - Ever burned a cake?
 *
 *  Copyright 2023 Emil Williams
 *
 *  baked is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3
 *  as published by the Free Software Foundation.
 *
 *  You should have received a copy of the GNU General Public License
 *  version 3 along with this program. If not, see <https://www.gnu.org/licenses/gpl-3.0.en.html>.
 *
 * This program is independent of language, it simply expects the notation as listed below,
 *
 * EXEC:cc $@ -o $* -std=gnu99 -O2 -Wall -Wextra -Wpedantic -pipe $CFLAGS:STOP
 *
 * Which will run, and in this case, result in this program compiling itself.
 *
 * Another (functional) example:
 EXEC:
 CFLAGS='-std=gnu99 -O2 -Wall -Wextra -Wpedantic -pipe'
 cc $@ -o $* $CFLAGS # baked
 :STOP
 *
 * See install.sh for another example coded into a different language.
 * It is possible to override environmental variables, as in install.sh,
 * you can then have some dynamism.
 *
 * Realistically, any traditional shell script could be composed in this
 * fashion, and be embedded in any file. However, some programming languages
 * may not have multiline comments, so you may be restricted to a single line.
 *
 * with the flag CFLAGS='-DSHAKE_COMPAT' Shake compatibility will be enabled,
 * @COMPILECMD cc $@ -o $* -std=gnu99 -O2 -Wall -Wextra -Wpedantic -pipe $CFLAGS # SHAKE_COMPAT enabled
 *
 * TODO
 *
 * 1. replace asserts with proper checks (maybe longjmp, however
 *    this might be costly, and might not satify -fanalyzer)
 * 2. somehow convince the compiler to do NULL checks before g_...
 *    so that allocs can be minimized even further (maybe volatile)
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#ifndef SHAKE_COMPAT
# define HELP                                                       \
  "target-file\n"                                                   \
  "Use the format `EXEC:command ...:STOP' within the target-file\n"
# define START "EXEC:"
# define STOP ":STOP"
#else
# define HELP                                                       \
  "target-file\n"                                                   \
  "Use the format `@COMPILECMD command ...\n' within the target-file\n"
# define START "@COMPILECMD "
# define STOP "\n"
#endif

#define DESC                                                          \
  "\t$@  returns target-file value\n"                                 \
  "\t$*  returns target-file without extension\n"                     \
  "\t$+  returns arguments\n"

#define die(fmt, ...) do { _die(fmt, ##__VA_ARGS__); goto stop; } while (0)

static const char * argv0;
static char * g_filename = NULL, * g_sh = NULL, * g_all = NULL;

static void
_die(const char * fmt,
    ...)
{
  va_list ap;
  assert(fmt);
  fprintf(stderr, "%s: ", argv0);
  va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (fmt[0] &&
      fmt[strlen(fmt)-1] == ':')
  {
		fputc(' ', stderr);
		perror(NULL);
	}
  else
  { fputc('\n', stderr); }
}

static int
exists(const char * fn)
{
  struct stat buf;
  if (!stat(fn,&buf) &&
      buf.st_mode & S_IFREG)
  { return 1; }
  return 0;
}

static char *
load(const char * fn)
{
  size_t len;
  char * buf;
  FILE * fp = fopen(fn, "rb");
  if (fp)
  {
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);
    buf = malloc(len + 1);
    if (fread(buf, 1, len, fp) > strlen(STOP) + strlen(START))
    {
      buf[len] = '\0';
      fclose(fp);
      return buf;
    }
    fclose(fp);
    free(buf);
  }
  return NULL;
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
  /* fprintf(stderr, "params '%s' '%s' %ld %ld\n", new, str, offset, shift); */
  str = realloc(str, max + len);
  assert(str);
  memmove(str + offset + len, str + offset + shift, max - offset - shift);
  memcpy(str + offset, new, len);
  return str;
}

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
expand(char * buf, size_t len)
{
  size_t i = 0;
  char * str = "";
  assert(buf);
  for (i = 0; i < len; ++i)
  {
    if (buf[i] == '\\')
    { i+=2; continue; }
    if (buf[i] == '$')
    {
      switch (buf[++i])
      {
      case '@': /* replace $@ with g_filename */
        str = g_filename;
        break;
      case '*': /* replace $* with short nm */
        str = g_sh;
        break;
      case '+': /* replace $+ with all args */
        str = g_all;
        break;
      default: continue;
      }
      buf = insert(str, buf, i - 1, 2);
      len = strlen(buf);
    }
  }
  return buf;
}

static void
init(char ** argv, int argc)
{
  size_t i, len;

  /* g_filename */
  g_filename = malloc(strlen(argv[1]) + 1);
  strcpy(g_filename, argv[1]);

  /* sh */
  { size_t last = 0;
  len = strlen(argv[1]); /* co-opting len */
  g_sh = malloc(len + 1);
  for (i = 0; i < len; ++i)
  {
    if (argv[1][i] == '.')
    { last = i; }
  }
  last = last ? last : i;
  strncpy(g_sh, argv[1], last);
  g_sh[last] = '\0';
  } /* EOL last */
  /* all */
  if (argc > 2)
  {
    len = 0;
    i = 2;
    while (i < (size_t) argc)
    {
      len += strlen(argv[i]);
      ++i;
    }
    g_all = malloc(len + 1);
    g_all[len] = '\0';
    len = 0;
    i = 2;
    while (i < (size_t) argc)
    {
      strcpy(g_all + len, argv[i]);
      len += strlen(argv[i]);
      ++i;
    }
  }
}

int
main(int argc, char ** argv)
{
  int ret = 0;
  char * buf = NULL;
  { size_t len;
  { char * start, * stop;

  argv0 = argv[0];

  if (argc < 2)
  { die(HELP); }

  if (!exists(argv[1]))
  { die("cannot access '%s':", argv[1]); }

  if (!(buf = load(argv[1]))
      ||          root(argv[1]))
  { die(errno ? NULL : "File too short"); }

  if (!(start = find(buf,   START))
      ||  !(stop  = find(start, STOP)))
  { die("No usable format located in '%s'", argv[1]); }

  init(argv, argc);

  len = stop - start - strlen(STOP);
  memmove(buf, start, len);
  } /* EOL start stop*/
  buf = realloc(buf, len + 1);
  buf[len] = '\0';
  fprintf(stderr, "Exec: %s\nOutput:\n", buf);
  buf = expand(buf, len);
  } /* EOL len */
  fprintf(stderr, "Result: %d\n", (ret = system(buf)));

stop:
  free(g_filename);
  free(g_sh);
  free(g_all);
  free(buf);
  return ret;
}
