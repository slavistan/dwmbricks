/* Maximum storage size of a command's output excl. null-delim */
#define OUTBUFSIZE 32

/* Bricks declaration */
const Brick bricks[] = {
  /* Command          , Update Interval , Tag */
  {"status backlight" , 0               , "backlight"} ,
  {"status keymap"    , 0               , "keymap"}    ,
  {"status power"     , 60              , "power"}     ,
  {"status systime"   , 1               , "time"}      ,
};

/* Delimiter between command ouputs */
const static char delim[] = " ｜ ";
