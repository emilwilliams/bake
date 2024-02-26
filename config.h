/* Require space after START */
#define REQUIRE_SPACE

#define ENABLE_COLOR

#ifdef ENABLE_COLOR
# define    RED "\033[91m"
# define  GREEN "\033[92m"
# define YELLOW "\033[93m"
# define    DIM "\033[2m"
# define   BOLD "\033[1m"
# define  RESET "\033[0m"
#else
# define    RED
# define  GREEN
# define YELLOW
# define    DIM
# define   BOLD
# define  RESET
#endif
