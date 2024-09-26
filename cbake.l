/* cbake.l @BAKE flex @FILE && cc -std=c99 -D_GNU_SOURCE -o @{@SHORT} lex.yy.c @ARGS -lfl @STOP */
%{
char * filename;
int gac; char ** gav;
int gmax = 0, line = 1, expunge_depth = 0;

extern void args(int n);
extern void shorten(char * filename, int n);
%}
%x FOUND PADDING
%option nodefault noinput noyywrap
%%
<FOUND>{
 @FILENAME|@FILE|$@  { printf(filename); }
 @SHORT:[[:digit:]]+ { shorten(filename, atoi(strrchr(yytext, ':')+1)); }
 @SHORT|$\*          { shorten(filename, 1); }
 @ARGS:[[:digit:]]+  { args(atoi(strrchr(yytext, ':')+1)); }
 @ARGS|$\+           { args(-1); }
 @LINE               { printf("%d", line); }
 @BAKE               { yyless(yyleng - strlen("@BAKE")); BEGIN INITIAL; }
 @STOP               { printf("@STOP\n"); BEGIN INITIAL; }
 \\[@$]              { putchar(yytext[1]); }
 \\\n                { ++line; }
 @\{                 { ++expunge_depth; }
 \}                  { if (!expunge_depth--) { ECHO; } }
 \n                  { ++line; putchar(yytext[0]); }
 [ \v\t\r\f]         { putchar(' '); BEGIN PADDING; }
 .                   { putchar(yytext[0]); }
}
<PADDING>{
  [ \v\t\r\f] { ; }
  .|\n        { yyless(0); BEGIN FOUND; }
}
\\[^\n]          { ; }
\\\n             { ++line; }
@BAKE[ \t\r\v\f] { goto bake; }
@BAKE\n          { ++line; bake: ++gmax; printf("@BAKE "); BEGIN FOUND; }
\n               { ++line; }
.                { ; }
%%

#include <ctype.h>

void help(void) { puts("See bake(1) - \"Buy high. Sell low.\"\n"); }

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
  if (n < 0) { for (int i = 0; i < gac; ++i) { printf(gav[i]); if (i + 1 < gac) { putchar(' '); } } }
  else if (n < gac) { printf(gav[n]); }
}

void shorten(char * filename, int n) {
  char * end = filename + strlen(filename);
  while (n && (end = memrchr(filename, '.', end - filename))) { --n; }
  if (!end) { fprintf(stderr, "<SHORTEN> context error: Argument out of range.\n"); printf("idiot"); return; }
  fwrite(filename, 1, end - filename, stdout);
}

int main (int ac, char ** av) {
  int run = 1, select = 1, list = 0;
  char * av0 = av[0];
  FILE * fp;
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
        opt_select:  case 's': select = atoi(av[0]+2+(av[0][2] == '=')); i = strlen(av[0]); break;
        opt_list:    case 'l': run = 0; select = -1; break;
        opt_help:    case 'h': help(); return 0;
        opt_default: default: fprintf(stderr, "%s: Unknown option '%s'\n", av0, av[0]); return 1;
        opt_arg:              fprintf(stderr, "%s: Argument missing for '%s'\n", av0, av[0]); return 1;
      }
    }
  }
start:
  if (!ac) { fprintf(stderr, "%s: Missing filename\n", av0); return 1; }

  /* filename and renaming */
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
  {
    FILE * bp = fmemopen(buf, 1 << 16, "wb+"), * stdout_old = stdout;
    stdout = bp;
    yylex();
    stdout = stdout_old;
    fclose(bp);
  }
  if (select > gmax) { fprintf(stderr, "%s: Out of range\n", av0); return 1; }
  if (!list) {
    fprintf(stderr, "%s: ", av0); fflush(stderr);
    fwrite(buf,1,1000,stdout);
    if (run) {
      FILE * gpipe = popen("/bin/sh -e", "w");
      if (!gpipe) { fprintf(stderr, "%s: <gpipe> %s\n", av0, strerror(errno)); return 1; }
      run = pclose(gpipe); if (run) { printf("%s: Exit code %d\n", av0, run); } return run;
    }
  }

}
