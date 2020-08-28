/* Maximum storage size of a command's output excl. null-delim */
#define OUTBUFSIZE 32

/* Logfile */
const char logfile[] = "/tmp/dwmbricks-log";

/* File containing the daemon's pid */
const char pidfile[] = "/tmp/dwmbricks-pid";

/* Bricks declaration */
const Brick bricks[] = {
  /* Command          , Update Interval , Tag */
  {"status backlight" , 0               , "backlight"} ,
  {"status keymap"    , 0               , "xkb"}       ,
  {"status power"     , 60              , "power"}     ,
  {"status systime"   , 1               , "time"}      ,
};

/* Delimiter between command ouputs */
const static char delim[] = " ｜ ";
