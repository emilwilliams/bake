/* static int color = ENABLE_COLOR; */

#if ENABLE_COLOR
# define    RED "\033[91m"
# define  GREEN "\033[92m"
# define YELLOW "\033[93m"
# define   BOLD "\033[1m"
# define  RESET "\033[0m"
#else
# define    RED
# define  GREEN
# define YELLOW
# define   BOLD
# define  RESET
#endif

#if 0

# define color_printf(...) color_fprintf (stdout, __VA_ARGS__)
/* not perfect, too simple, doesn't work with a var, only a literal. */
# define color_fputs(fp, msg) color_fprintf (fp, msg "\n")
# define color_puts(msg) color_fputs (stdout, msg)

static void
color_fprintf (FILE * fp, char * format, ...) {
  va_list ap;
  char * buf;

  va_start (ap, format);

  if (color) {
    vfprintf (fp, format, ap);
    va_end (ap);
    return;
  }
  vasprintf (&buf, format, ap);

  if (buf) {
    /* char * expanded, * colors [] = { */
      /* YELLOW, "", */
      /* GREEN,  "", */
      /* RED,    "", */
      /* BOLD,   "", */
      /* RESET,  "" */
    /* }; */
    /* size_t count = ARRLEN (colors); */
    /* expanded = expand (buf, strlen (buf), colors, count - 2); */
    /* expanded = expand (expanded, strlen (buf), colors + count - 2, 2); */
    /* fwrite (expanded, strlen (expanded), 1, fp); */
  }

  free (buf);
  va_end (ap);
}

#endif
