/* Maximum storage size of a command's output excl. null-delim */
#define OUTBUFSIZE 32

/* Bricks declaration */
const Brick bricks[] = {
  /* Command          , Update Interval , Tag */
  {"status backlight" , 0               , "backlight"} ,
  {"status keymap"    , 0               , "keymap"}    ,
  {"status power"     , 60              , "power"}     ,
  {"status systime"   , 1               , "time"}      ,
  {"status dummy"     , 0               , "dummy"}     ,
};

/* Delimiter between command ouputs */
const static char delim[] = " ｜ ";

/* Daemon's pid-file's full path. '%d' will be replaced by the user id */
#define PIDFILEPATH_TEMPLATE "/tmp/dwmbricks-pid-%d"

/**
 * Misc compile-time settings
 */

/* Max. size of daemon's pid-file's name in bytes */
#define PIDFILEPATH_MAXLEN 64

#define shmsz 4096
