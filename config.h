/* Maximum size of a command's stdout (in bytes and excl. null-delim) */
#define OUTBUFSIZE 32

/* File containing the daemon's pid */
#define PIDFILEPATH "/tmp/dwmbricks-pid"

/* Bricks declaration */
static const Brick bricks[] = {
  /* Command        , Update Interval , Update Signal , Name */
  {"status ding"    , 0               , 4             , "test"}  ,
  {"status keymap"  , 0               , 1             , "xkb"}   ,
  {"status power"   , 60              , 2             , "power"} ,
  {"status systime" , 60              , 3             , "time"}  ,
};

/* Delimiter between bricks. */
static char delim[] = " | "; // TIL `char *arr != char arr[]'
