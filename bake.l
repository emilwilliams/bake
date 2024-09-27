/* cbake.l @BAKE flex @FILE && cc -Wall -Wextra -std=c99 -D_GNU_SOURCE -o @SHORT lex.yy.c @ARGS -lfl @STOP */
/* TODO: implement expunge, color */
%{
#include <ctype.h>

#undef ECHO
#define ECHO             do { fprintf(stdout, yytext);      if (g_pipe) { fprintf(g_pipe, yytext); }      } while (0)
#define CHAR(c)          do { fputc(c, stdout);             if (g_pipe) { fputc(c, g_pipe); }             } while (0)
#define STRING(s)        do { fputs(s, stdout);             if (g_pipe) { fputs(s, g_pipe); }             } while (0)
#define FORMAT(...)      do { fprintf(stdout, __VA_ARGS__); if (g_pipe) { fprintf(g_pipe, __VA_ARGS__); } } while (0)
#define FWRITE(str, len) do { fwrite(str, 1, len, stdout);  if (g_pipe) { fwrite(str, 1, len, g_pipe); }  } while (0)

/* input from main to lexer */
FILE * g_pipe;
char * g_filename;
int g_ac;
char ** g_av;
int g_select = 1;
/* for the lexers eyes only */
int line = 1, nth = 0, expunge_depth = 0, first_nl, tmpline;

extern void root(char * filename);
extern void args(int n);
extern void shorten(char * filename, int n);
%}

SPACE [ \t\r\v\f]
MACROS (@BAKE|@FILENAME|@FILE|@NAME|@SHORT|@ARGS|@LINE|@STOP|$@|$*|$+)

%x FOUND PADDING STOP
%option nodefault noinput nounput noyywrap
%%

@BAKE[[:space:]] { bake:
  first_nl = 1;

  if (yytext[yyleng-1] == '\n') { ++line; }
  if (!g_select) { ; }
  else if (g_select < 0) { BEGIN FOUND; printf("\n%s:%d:s%d: ", g_filename, line, ++nth); }
  else if (!--g_select)  { BEGIN FOUND;                                                   }
}

\n { ++line; }
.  {;}

<FOUND>{
 @BAKE[[:space:]]|@STOP   { BEGIN INITIAL; yyless(0); if (first_nl) { CHAR('\n'); } if (!g_select) { return 0; }    }
 @FILENAME|@FILE|@NAME|$@ { STRING(g_filename);                                                                   }
 @SHORT:[[:digit:]]+      { shorten(g_filename, atoi(strrchr(yytext, ':')+1));                                      }
 @SHORT|$\*               { shorten(g_filename, 1);                                                                 }
 @ARGS:[[:digit:]]+       { args(atoi(strrchr(yytext, ':')+1));                                                     }
 @ARGS|$\+                { args(-1);                                                                               }
 @LINE                    { FORMAT("%d", line);                                                                     }
 @\{                      { ++expunge_depth;                                                                        }
 \}                       { if (!expunge_depth--) { ECHO; }                                                         }
 \\\n                     { BEGIN PADDING; ++line; CHAR(' ');                                                       }
 \\{MACROS}               { STRING(yytext + 1);                                                                     }
 \n                       { CHAR('\n'); ++line; if (first_nl) { BEGIN STOP; first_nl = 0; tmpline = 0; }            }
 {SPACE}                  { BEGIN PADDING; CHAR(' ');                                                               }
 .                        { ECHO;                                                                                   }
}

<PADDING>{
  {SPACE} {                       ; }
  .|\n    { yyless(0); BEGIN FOUND; }
}

<STOP>{
 @BAKE[[:space:]] { line += tmpline; goto bake; }
 @STOP            { BEGIN FOUND; yyless(0);     }
 \n               { ++tmpline;   yymore();      }
 .|\\@            {              yymore();      }
}

%%

void root(char * filename) {
  char * path, * terminator;
  if (!(path = realpath(filename, NULL))) { return; }
  if ((terminator = strrchr(path, '/'))) {
    *terminator = '\0';
    chroot(path);
  }
  free(path);
}

void args(int n) {
  if (n < 0) { for (int i = 0; i < g_ac; ++i) { STRING(g_av[i]); if (i + 1 < g_ac) { CHAR(' '); } } }
  else if (n < g_ac) { STRING(g_av[n]); }
}

void shorten(char * filename, int n) {
  char * end = filename + strlen(filename);
  while (n && (end = memrchr(filename, '.', end - filename))) { --n; }
  if (!end) {
    fprintf(stderr, "<SHORTEN> context error: Argument out of range.\n");
    /* Ensures consistency. @SHORT will always return *something* that isn't filename */
    STRING("idiot");
    return;
  }
  FWRITE(filename, end - filename);
}

void help(void) { fputs("see bake(1) - \"Buy high. Sell low.\"\n", stderr); }

int main (int ac, char ** av) {
  int run = 1;
  char * av0 = av[0];
  FILE * fp;

  /* supports long/short, -allinone, (-X ... -X=... -X<NUM>) */
  while (++av, --ac) {
    size_t i;
    if (av[0][0] != '-') { goto start; }
    if (av[0][1] == '-') {
      if (av[0][2] == '\0')            { ++av, --ac;                       goto start;       }
      if (!strcmp(av[0]+2, "dry-run")) { i = strlen(av[0]);                goto opt_dry_run; }
      if (!strcmp(av[0]+2, "color"))   { i = strlen(av[0]);                goto opt_color;   }
      if (!strcmp(av[0]+2, "expunge"))   { i = strlen(av[0]);              goto opt_expunge; }
      if (!strcmp(av[0]+2, "select" )) { if (!ac-1 || isdigit(av[1][0])) { goto opt_arg;     }
                                         ++av, --ac; i = strlen(av[0]);    goto opt_select;  }
      if (!strcmp(av[0]+2, "list"   )) { i = strlen(av[0]);                goto opt_list;    }
      if (!strcmp(av[0]+2, "help"   )) {                                   goto opt_help;    }
      goto opt_default;
    }
    for (i = 1; i < strlen(av[0]); ++i) {
      switch (av[0][i]) {
        opt_dry_run: case 'n': run = 0; break;
          case 's':
          /* Covers cases -<LAST>s<NUM> -<LAST>s <NUM> */
               if (isdigit(av[0][i+1]))         { g_select = atoi(av[0]+i+1); }
          else if (ac > 1 && isdigit(av[1][0])) { ++av, --ac; opt_select: g_select = atoi(av[0]); }
          else                                  { g_select = 0; }
               if (!g_select)                   { fprintf(stderr, "%s: Invalid argument for -s\n", av0); return 1; }
          i = strlen(av[0]);
          break;
        opt_list:    case 'l': run = 0; g_select = -1; break;
        opt_help:    case 'h': help(); return 0;
        opt_color:   case 'c': break;
        opt_expunge: case 'x': break;
        opt_default:  default: fprintf(stderr, "%s: Unknown option '%s'\n", av0, av[0]); return 1;
        opt_arg:               fprintf(stderr, "%s: Argument missing for '%s'\n", av0, av[0]); return 1;
      }
    }
  }

 start:
  if (!ac) { fprintf(stderr, "%s: Missing filename\n", av0); return 1; }
  if (!g_select) { goto out_of_range; }

  g_filename = av[0];
  root(g_filename);
  { /* ensures the filename doesn't have a relative path that would misdirect the command within the new root */
    char * tmp = strrchr(g_filename, '/');
    if (tmp) { g_filename = tmp+1; }
  }

  /* open and prepare ac, av */
  if (!(yyin = fp = fopen(g_filename, "rb")))
  { fprintf(stderr, "%s: '%s' %s\n", av0, g_filename, strerror(errno)); return 1; }
  g_ac = --ac, g_av = ++av;

  /* Prepares our UNIX pipe for input */
  if (run) {
    g_pipe = popen("/bin/sh -e", "w");
    if (!g_pipe) { fprintf(stderr, "%s: <g_pipe> %s\n", av0, strerror(errno)); return 1; }
  }

  if (g_select > 0) { fprintf(stderr, "%s: ", av0); fflush(stderr); }
  yylex(); fflush(stdout);
  fclose(fp);
  if (g_select > 0) { pclose(g_pipe); goto out_of_range; }

  if (!run) { return 0; }
  fprintf(stderr, "output: "); fflush(stderr);
  run = pclose(g_pipe); /* repurposed run */
  putchar('\n');
  if (run) { printf("%s: Exit code %d\n", av0, run); }
  return run;
  out_of_range: fprintf(stderr, "%s: <%d> Out of range\n", av0, g_select); return 1;
}
