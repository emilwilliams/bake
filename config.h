/* Require space after START */
#define REQUIRE_SPACE

#define ENABLE_COLOR

#ifdef ENABLE_COLOR
# define    RED "\e[91m"
# define  GREEN "\e[92m"
# define YELLOW "\e[93m"
# define    DIM "\e[2m"
# define   BOLD "\e[1m"
# define  RESET "\e[0m"
#else
# define    RED
# define  GREEN
# define YELLOW
# define    DIM
# define   BOLD
# define  RESET
#endif
