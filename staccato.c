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
static pid_t daemonpid; /* Daemon's PID */
static char pidfilepath[PIDFILEPATH_MAXLEN];
static char lockfilepath[LOCKFILEPATH_MAXLEN]; /* path to cli lock file */
static int lockfilefd;

static void cleanup(int);
static void siginstr(unsigned, unsigned);
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

void
siginstr(unsigned instrndx, unsigned envcount)
{
  union sigval sv = { .sival_int = 0 };
  unsigned *pdata = &sv.sival_int;
  *pdata = (envcount << (sizeof(unsigned) * CHAR_BIT - 3)) | instrndx;
  sigqueue(daemonpid, SIGUSR1, sv);
}

/*
 * TODO: adjust doc
 * Signal the daemon to execute instr to which the UTF-8 character at index
 * 'charndx' belongs. Character index and optional mouse button are encoded
 * into the signal's data. Elicits SIGURS2. Called exclusively by the cli.
 */
void
sigchar(unsigned charndx, unsigned envcount)
{
  union sigval sv = { .sival_int = 0 };
  unsigned *pdata = &sv.sival_int;
  *pdata = (envcount << (sizeof(unsigned) * CHAR_BIT - 3)) | charndx;
  sigqueue(daemonpid, SIGUSR2, sv);
}

int
main(int argc, char** argv) {

  unsigned envcount;
  int ii;
  key_t key;
  char buf[16];
  FILE* file;

  /* Runtime init */
  sprintf(lockfilepath, LOCKFILEPATH_TEMPLATE, getuid()); // TODO: error checking
  sprintf(pidfilepath, PIDFILEPATH_TEMPLATE, getuid()); // TODO: error checking
  if (!(file = fopen(pidfilepath, "r"))) {
    elog("Cannot open PID-file:");
    exit(1);
  }
  if (!fgets(buf, sizeof(buf), file)) { // TODO: proper error checking of fgets
    elog("Cannot read from PID-file:");
    exit(1);
  }
  if (fclose(file)) {
    elog("Cannot fclose() PID-file '%s':");
    exit(1);
  }
  daemonpid = (pid_t)strtoul(buf, NULL, 10);

  /* Sanity check */
  // TODO: Check if daemon is running


  /* lock to concurrent shmem accesses */
  // TODO: Wait for unlock-file-fd event instead of using sleep()
  while ((lockfilefd = open(lockfilepath, O_CREAT|O_EXCL, O_RDWR)) == -1) {
    sleep(1);
  }

  /* Signal init */
  signal(SIGQUIT, cleanup);
  signal(SIGTERM, cleanup);
  signal(SIGINT, cleanup);
  signal(SIGSEGV, cleanup);
  signal(SIGHUP, cleanup);

  /* Parse trailing envvar args */
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
    for (ii = 0; ii < LENGTH(instructions); ii++) {
      if (!strcmp(argv[2], instructions[ii].tag)) {
        siginstr(ii, envcount);
      }
    }
  } else if (!strcmp(argv[1], "-i")) {
    ndx = strtoul(argv[2], NULL, 10);
    siginstr(ndx, envcount);
  } else {
    elog("Invalid arguments.");
  }

  cleanup(0);
  return 0;
}
