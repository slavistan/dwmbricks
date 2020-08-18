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
  printf("pid = %u\n", pid);
  return pid;
}

/*
 * Terminate daemon.
 */
void daemonkill(void) {
  kill(getpid(), SIGTERM);
}

/*
 * Send user signal to daemon.
 */
void
daemonkickbysignal(pid_t usersigno) {
  assert(0 <= usersigno && usersigno <= (SIGRTMAX - SIGRTMIN));

  kill(getpid(), SIGRTMIN + usersigno);
}

/*
 //... asd
 */
void
daemonkickbyname(const char* name) {
  for (int ii = 0; ii < numbricks; ++ii) {
    if (!strcmp(name, (bricks + ii)->tag)) {
      daemonkickbyindex(ii);
    }
  }
}

void
daemonkickbycharindex(unsigned int index /* 0-indexed */) {
  assert(index < 256); /* Index shall fit in a single byte */ 

  unsigned int b;
  char *penv;

  /* Retrieve $BUTTON from environment (valid values are {1, 2, ..}) */
  if ((penv = getenv("BUTTON")) != NULL) {
    b = strtoul(penv, NULL, 10); // 0 if invalid, no need to check
  }
  /* Encode $BUTTON and index into payload */
  int payload = (index & 0xFF) | ((b & 0xFF) << 8);

  /* Fire */
  union sigval sv;
  sv.sival_int = payload;
  sigqueue(getpid(), SIGUSR1, sv);
}


/*
 * Trigger execution of a brick's command in the running daemon.
 * Sends the according brick's signal to the daemon.
 */
void
daemonkickbyindex(unsigned int bindex) {
  assert(bindex < numbricks);

  /* TODO: Encode brickid into signal payload and remove RT signals. */

  daemonkickbysignal((bricks + bindex)->signal);
}


void
cli_kick(void *ptr, int what)
{
  int payload;
  switch (what) {
    case BRICK:
      payload = ((unsigned int)BRICK << 8) & *((unsigned int*)ptr);
      // send signal
      break;

    case CHAR:
      payload = *((unsigned int*)ptr);
      // send signal
      break;

    case TAG:
      // get brick
      // send signal

      break;
  }
}
