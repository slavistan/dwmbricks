/**
 * User configuration
 */

/* Persistent instructions */
const Instruction instructions[] = {
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
#define PIDFILEPATH_TEMPLATE "/tmp/staccato-pid-%d"

/* Cli lockfile's full path. '%d' will be replaced by the user id */
#define LOCKFILEPATH_TEMPLATE "/tmp/staccato-lock-%d"

/* Maximum size of a command's output excl. null-delim in bytes. Longer
 * outputs are truncated. */
#define OUTBUFSIZE 64

/* Max. size of daemon's pid-file's name in bytes */
#define PIDFILEPATH_MAXLEN 64

/* Max. size of cli's lockfile's name in bytes */
#define LOCKFILEPATH_MAXLEN 64

/* Size of shmem segment in bytes used to pass envvar strings to daemon */
#define shmsz 1024
