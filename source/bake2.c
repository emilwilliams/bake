/* @BAKE cc -I. -std=c99 -O2 -Wall -Wextra -Wpedantic -Wno-parentheses -Wno-implicit-fallthrough -o @SHORT sds.c @FILENAME @ARGS @STOP */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sds.h>

#define ARRLEN(x) (sizeof (x) / sizeof (x [0]))

#define START "@" "BAKE" " "
#define STOP  "@" "STOP"

#define ENABLE_COLOR 1

#include "color.h"

static int map(char * filename, char ** buffer, size_t * buffer_length) {
  int err = 0, fd = open(filename, O_RDONLY);
  if (fd != -1) {
    struct stat s;
    if (!fstat(fd, &s) && s.st_mode & S_IFREG && s.st_size) {
      *buffer_length = (size_t) s.st_size;
      *buffer = (char *) mmap(NULL, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
    } else {
      err = 1;
    }
    close(fd);
  }
  return err;
}

void root(const char * filename) {
  char * string = strdupa(filename), * terminator = strrchr(string, '/');
  if (terminator) {
    terminator = '\0';
    chdir(string);
  }
}

void err(char * msg) { fprintf(stderr, "%s\n", msg); abort(); }

void help(void) { abort(); }


void selection_list(char * buffer, size_t buffer_length, sds ** rlist, size_t * rlist_length) {
  sds * list = malloc(0);
  size_t list_length = 0;

  char * search = buffer;
  char * start;
  while (start = memmem(search, buffer_length, START, strlen(START))) {
    buffer_length -= start - buffer + strlen(START);
    search = start + strlen(START);
    char * end = memmem(search, buffer_length, STOP, strlen(STOP));
    if (!end) { end = memchr(search, '\n', buffer_length);}
    if (start && end) {
      list = realloc(list, ++list_length * sizeof(sds));
      list[list_length-1] = sdsnewlen(start,end-start);
    }
  }

  *rlist = list;
  *rlist_length = list_length;
}

int main(int argc, char ** argv) {
  char * filename = NULL;
  int run = 1, select = 0, olist = 0;

  for (int off = 1; off < argc; ++off) {
    if (argv [off][0] != '-') { filename = argv [off]; ++off; break; }
    if (argv [off][1] != '-') {
      while (*(++argv [off]))
      switch (argv [off][0]) {
      select:  case 's':
        select = atoi(argv [off] + 1);
        if (!select) {
          ++off;
          if (off >= argc) { help(); }
          select = atoi(argv [off]);
        }
        if (select) { goto next; }
      default: case 'h': help();
               case 'l': olist = 1;
               case 'n': run = 0;   break;
               /* case 'c': color = 0; break; */
               /* case 'x': ex = 1; break; */
               case 'q': fclose(stderr); fclose(stdout); break;
      }
      continue;
    }
    argv[off] += 2;
         if (strcmp(argv[off],  "select") == 0) { goto select; }
    else if (strcmp(argv[off],    "list") == 0) { olist = 1; run = 0; }
    else if (strcmp(argv[off], "dry-run") == 0) { run = 0; }
    /* else if (strcmp(argv[off],   "color") == 0) { color = 0; } */
    /* else if (strcmp(argv[off], "expunge") == 0) { ex = 1; } */
    else if (strcmp(argv[off],   "quiet") == 0) { fclose(stderr); fclose(stdout); }
    else if (strcmp(argv[off],    "help") == 0) { help(); }
  next:;
  }

  if (argc == 1 || !filename) { help(); }

  root(filename);

  char * buffer = NULL;
  size_t buffer_length = 0;

  if (map(filename, &buffer, &buffer_length) || !buffer) { err("Could not access file"); }

  /* select */
  sds * list;
  size_t list_length;
  selection_list(buffer, buffer_length, &list, &list_length);

  if (olist) {
    for (size_t i = 0; i < list_length; ++i) {
      printf("%s\n", list[i]);
  }}

  for (size_t i = 0; i < list_length; ++i) {
    if (olist || (size_t) select != i) { sdsfree(list[i]); }
  }

  sds command = list[select];
  free(list);

  if (olist) { return 0; }

  /* expand */

  char * macro [] = {
    "$*",
    "$+",
    "$@",
    "@ARGS",
    "@FILE",
    "@FILENAME",
    "@LINE"
    "@SHORT",
  };

  char * search;
  size_t search_length = sdslen(command);
  for (size_t i = 0; i < ARRLEN(macro); ++i) {
    search = command;
    while (search = memmem(search, search_length, macro[i], strlen(macro[i]))) {
      search += strlen(macro[i]);
      printf("Found %s\n", macro[i]);
    }
  }

  /* execute */
  printf("command: '%s'\n", command);
  sdsfree(command);

  if (!run) { return 0; }

  fprintf (stderr, GREEN "output" RESET ":\n");

  pid_t pid;
  if ((pid = fork ()) == 0) {
    execl ("/bin/sh", "sh", "-c", command, NULL);
    return 0; /* execl overwrites the process anyways */
  }

  if (pid == -1) {
    fprintf (stderr, GREEN "%s" RESET ": %s, %s\n", argv [0], "Fork Error", strerror (errno));
    return 1;
  }

  /* reuse of run as status return */
  if (waitpid (pid, &run, 0) < 0) {
    fprintf (stderr, GREEN "%s" RESET ": " RED "%s" RESET ", %s\n", argv [0], "Wait PID Error", strerror (errno));
    return 1;
  }

  if (!WIFEXITED (run)) {
    return 1;
  }

  return WEXITSTATUS (run);
}
