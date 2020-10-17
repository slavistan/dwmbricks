#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"
#include "config.h"

static char *shm; /* shmem pointer */
static int shmid = -1; /* shm segment id; -1 = uninitialized */
static char pidfilepath[PIDFILEPATH_MAXLEN];
static char lockfilepath[LOCKFILEPATH_MAXLEN]; /* path to cli lock file */
static int lockfilefd;

static void cleanup(int);
pid_t daemonpid(void);
static void sigbrick(unsigned, unsigned);
static void sigchar(unsigned, unsigned);

/*
 * Cleanup resources. Called exclusively by daemon.
 */
void
cleanup(int exitafter) {
  close(lockfilefd);
  remove(lockfilepath);

  if (exitafter)
    exit(0);
}

/*
 * Retrieve the running daemon's pid from pid-file.
 * TODO: Retrieve pid once and store in global. Remove this function.
 */
pid_t
daemonpid(void) {
  char buf[16];
  FILE* file;
  pid_t pid;

  if (!(file = fopen(pidfilepath, "r"))) {
    elog("Cannot open PID-file:");
    exit(1);
  }
  if (!fgets(buf, sizeof(buf), file)) {
    elog("Cannot read from PID-file:");
    exit(1);
  }
  if (fclose(file)) {
    elog("Cannot fclose() PID-file '%s':");
    exit(1);
  }
  return (pid_t)strtoul(buf, NULL, 10);
}

void
sigbrick(unsigned brickndx, unsigned envcount)
{
  union sigval sv = { .sival_int = 0 };
  unsigned *pdata = &sv.sival_int;
  *pdata = (envcount << (sizeof(unsigned) * CHAR_BIT - 3)) | brickndx;
  sigqueue(daemonpid(), SIGUSR1, sv);
}

/*
 * TODO: adjust doc
 * Signal the daemon to execute brick to which the UTF-8 character at index
 * 'charndx' belongs. Character index and optional mouse button are encoded
 * into the signal's data. Elicits SIGURS2. Called exclusively by the cli.
 */
void
sigchar(unsigned charndx, unsigned envcount)
{
  union sigval sv = { .sival_int = 0 };
  unsigned *pdata = &sv.sival_int;
  *pdata = (envcount << (sizeof(unsigned) * CHAR_BIT - 3)) | charndx;
  sigqueue(daemonpid(), SIGUSR2, sv);
}

int
main(int argc, char** argv) {

  unsigned envcount;
  int ii;
  key_t key;

  /* Runtime init */
  sprintf(lockfilepath, LOCKFILEPATH_TEMPLATE, getuid()); // TODO: error checking
  sprintf(pidfilepath, PIDFILEPATH_TEMPLATE, getuid()); // TODO: error checking

  /* Sanity check */
  // TODO: Check if daemon is running


  /* Signal init */
  signal(SIGQUIT, cleanup);
  signal(SIGTERM, cleanup);
  signal(SIGINT, cleanup);
  signal(SIGSEGV, cleanup);
  signal(SIGHUP, cleanup);

  if (argc >= 3) {
    /* ensure trailing args are pairs of `-e NAME=VALUE` */
    ii = 3;
    while (ii < argc) {
      if (argc <= ii+1 || strcmp(argv[ii], "-e")) {
        elog("Invalid arguments.");
        exit(1);
      }
      if (!strchr(argv[ii+1], '=')) {
        elog("Invalid arguments: Envvar string is not of the form 'NAME=VALUE'.");
        exit(1);
      }
      ii+=2;
    }

    /* lock to prevent multiple instances of cli running in parallel */
    ii = 0;
    while ((lockfilefd = open(lockfilepath, O_CREAT|O_EXCL, O_RDWR)) == -1) {
      sleep(1);
      if (ii >= 3) {
        elog("Lockfile '%s' exists and process timed out.", lockfilepath);
        return 1;
      }
      ++ii;
    }

    /* shmem init */
    if ((key = ftok(pidfilepath, 1)) < 0) {
      elog("ftok() failed:");
      cleanup(1);
    }
    if ((shmid = shmget(key, shmsz, 0)) < 0) {
      elog("shmget() failed:");
      cleanup(1);
    }
    if ((shm = (char*)shmat(shmid, NULL, 0)) == (char*)-1) {
      elog("shmat() failed:");
      cleanup(1);
    }

    /* paste envvar strings into shmem */
    envcount = (argc - 3) / 2;
    char *p = (char*)((ptrdiff_t*)shm + envcount + 1);
    ptrdiff_t *s = (ptrdiff_t*)shm;
    for (ii = 0; ii < envcount; ++ii) {
      *s = p - shm;
      p = stpcpy(p, argv[4 + ii*2]) + 1;
      s++;
    }
    *s = 0;
  }

  unsigned ndx;
  if (!strcmp(argv[1], "-c")) {
    ndx = strtoul(argv[2], NULL, 10);
    sigchar(ndx, envcount);
  } else if (!strcmp(argv[1], "-t")) {
    for (ii = 0; ii < LENGTH(bricks); ii++) {
      if (!strcmp(argv[2], bricks[ii].tag)) {
        sigbrick(ii, envcount);
      }
    }
  } else if (!strcmp(argv[1], "-b")) {
    ndx = strtoul(argv[2], NULL, 10);
    sigbrick(ndx, envcount);
  } else {
    elog("Invalid arguments.");
  }

  cleanup(0);
  return 0;
}
