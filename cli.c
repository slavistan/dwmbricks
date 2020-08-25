#include "cli.h"
#include "util.h"
#include "dwmbricks.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BRICK, CHAR, TAG };

/*
 * Retrieve the running daemon's pid from pid-file.
 */
pid_t getdaemonpid(void) {
  static char buf[16];
  FILE* pidfile = fopen(PIDFILEPATH, "r");
  if (!pidfile)
    die("Cannot open PID-file\n");
  if (!fgets(buf, 16, pidfile))
    die("Cannot read from PID-file\n");
  fclose(pidfile);
  pid_t pid = (pid_t)strtoul(buf, NULL, 10);
  return pid;
}

/*
 * Terminate daemon.
 */
void daemonkill(void) {
  kill(getdaemonpid(), SIGTERM);
}
