/* cbake.l @BAKE flex @FILE && cc -std=c99 -D_GNU_SOURCE -o @{@SHORT} lex.yy.c @ARGS -lfl @STOP */
%{

#include <ctype.h>

#undef ECHO
#define ECHO             do { fprintf(stdout, yytext);      if (gpipe) { fprintf(gpipe, yytext); }      } while (0)
#define CHAR(c)          do { fputc(c, stdout);             if (gpipe) { fputc(c, gpipe); }             } while (0)
#define STRING(s)        do { fputs(s, stdout);             if (gpipe) { fputs(s, gpipe); }             } while (0)
#define FORMAT(...)      do { fprintf(stdout, __VA_ARGS__); if (gpipe) { fprintf(gpipe, __VA_ARGS__); } } while (0)
#define FWRITE(str, len) do { fwrite(str, 1, len, stdout);  if (gpipe) { fwrite(str, 1, len, gpipe); }  } while (0)

FILE * gpipe;

char * filename;
int gac;
char ** gav;
/* Options */
int gselect = 1;
/* accum */
 int line = 1, expunge_depth = 0, first_nl,  nth = 0;

extern void root(char * filename);
extern void args(int n);
extern void shorten(char * filename, int n);
%}

SPACE [ \t\r\v\f]
NUM [[:digit:]]

%x FOUND STOP PADDING

MACROS (@BAKE|@FILENAME|@FILE|@SHORT|@ARGS|@LINE|@STOP|$@|$*|$+)

%option nodefault noinput noyywrap
%%

<FOUND>{
 /* New behavior */
 @BAKE              { if (!gselect) { return 0; } yyless(yyleng - strlen("@BAKE")); BEGIN INITIAL; }
 @FILENAME|@FILE|$@ { STRING(filename); }
 @SHORT:{NUM}+      { shorten(filename, atoi(strrchr(yytext, ':')+1)); }
 @SHORT|$\*         { shorten(filename, 1); }
 @ARGS:{NUM}+       { args(atoi(strrchr(yytext, ':')+1)); }
 @ARGS|$\+          { args(-1); }
 @LINE              { FORMAT("%d", line); }
 @STOP              { CHAR('\n'); if (!gselect) { return 0; } BEGIN INITIAL; }
 \\\n               { ++line; CHAR(' '); BEGIN PADDING; }
 \\{MACROS}         { putchar(yytext[1]); }
 @\{                { ++expunge_depth; }
 \}                 { if (!expunge_depth--) { ECHO; } }
 \n                 { ++line; if (first_nl) { first_nl = 0; BEGIN STOP; } }
 {SPACE}            { CHAR(' '); BEGIN PADDING; }
 .                  { ECHO; }
}

<STOP>{
 @BAKE{SPACE} {
  first_nl = 1;
  if (gselect < 0) { printf("\n%s:%d s%d: ", filename, line, ++nth); }
  BEGIN FOUND;
 }
 @STOP { yyless(-1); BEGIN FOUND; }
 \n    { yymore(); ++line; }
 .|\\@ { yymore(); }
}

<PADDING>{
  {SPACE} { ; }
  .|\n    { yyless(0); BEGIN FOUND; }
}

@BAKE[[:space:]] {
  first_nl = 1;
  if (gselect <  0) { printf("%s:%d s%d: ", filename, line, ++nth); BEGIN FOUND; }
  else if (gselect == 0) { BEGIN FOUND; }
  else { --gselect; }
}

\n           { ++line; }
.            { ; }

%%

void root(char * filename) {
  char * path, * terminator;
  if (!(path = realpath(filename, NULL))) { return; }
  if (terminator = strrchr(path, '/')) {
    *terminator = '\0';
    chroot(path);
  }
  free(path);
}

void args(int n) {
  if (n < 0) { for (int i = 0; i < gac; ++i) { STRING(gav[i]); if (i + 1 < gac) { CHAR(' '); } } }
  else if (n < gac) { STRING(gav[n]); }
}

void shorten(char * filename, int n) {
  char * end = filename + strlen(filename);
  while (n && (end = memrchr(filename, '.', end - filename))) { --n; }
  if (!end) { fprintf(stderr, "<SHORTEN> context error: Argument out of range.\n"); STRING("idiot"); return; }
  FWRITE(filename, end - filename);
}

#define HELP                                                                               \
  "bake(1) - \"Buy high. Sell low.\"\n"
void help(void) { puts(HELP); }

int main (int ac, char ** av) {
  int run = 1;
  char * av0 = av[0];
  FILE * fp, * op;

  while (++av, --ac) {
    int i;
    if (av[0][0] != '-') { goto start; }
    if (av[0][1] == '-') {
      if (av[0][2] == '\0')            { ++av, --ac; goto start; }
      if (!strcmp(av[0]+2, "dry-run")) { i = strlen(av[0]); goto opt_dry_run; }
      if (!strcmp(av[0]+2, "select" )) { if (!ac-1 || isdigit(av[1][0])) { goto opt_arg; } ++av, --ac;
                                         i = strlen(av[0]); goto opt_select; }
      if (!strcmp(av[0]+2, "list"   )) { i = strlen(av[0]); goto opt_list; }
      if (!strcmp(av[0]+2, "help"   )) { goto opt_help; }
      goto opt_default;
    }
    for (i = 1; i < strlen(av[0]); ++i) {
      switch (av[0][i]) {
        opt_dry_run: case 'n': run = 0; break;
        opt_select:  case 's': gselect = atoi(av[0]+2+(av[0][2] == '=')); i = strlen(av[0]); break;
        opt_list:    case 'l': run = 0; gselect = -1; break;
        opt_help:    case 'h': help(); return 0;
        opt_default: default: fprintf(stderr, "%s: Unknown option '%s'\n", av0, av[0]); return 1;
        opt_arg:              fprintf(stderr, "%s: Argument missing for '%s'\n", av0, av[0]); return 1;
      }
    }
  }

 start:
  if (!ac) { fprintf(stderr, "%s: Missing filename\n", av0); return 1; }
  if (gselect == 0) { fprintf(stderr, "%s: Out of range\n", av0); return 1; }

  /* filename and self placement */
  filename = av[0];
  root(filename);
  {
    char * tmp = strrchr(filename, '/');
    if (tmp) { filename = tmp+1; }
  }

  /* open and prepare ac, av */
  if (!(yyin = fp = fopen(filename, "rb")))
  { fprintf(stderr, "%s: '%s' %s\n", av0, filename, strerror(errno)); return 1; }
  gac = --ac, gav = ++av;

  if (run) {
    gpipe = popen("/bin/sh -e", "w");
    if (!gpipe) { fprintf(stderr, "%s: <gpipe> %s\n", av0, strerror(errno)); return 1; }
  }

  if (gselect > 0) { fprintf(stderr, "%s: ", av0); fflush(stderr); }
  yylex();
  /* putchar('\n'); */
  fclose(fp);
  if (gselect > 0) { fprintf(stderr, "%s: Out of range\n", av0); }

  if (!run) { return 0; }
  fprintf(stderr, "output: "); fflush(stderr);
  run = pclose(gpipe);
  /* repurposed run */
  if (run) { printf("%s: Exit code %d\n", av0, run); }
  return run;
}
