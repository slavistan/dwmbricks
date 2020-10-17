#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"
#include "config.h"

static void brickexec(unsigned, unsigned);
static int brickfromchar(unsigned);
static void cleanup(int);
static void collectflush(void);

static void usr1(int, siginfo_t*, void*);
static void usr2(int, siginfo_t*, void*);

static char cmdoutbuf[LENGTH(bricks)][OUTBUFSIZE + 1] = {0}; /* per-brick stdout buffer */
static unsigned delimlen; /* number of utf8 chars in delim */
static char pidfilepath[PIDFILEPATH_MAXLEN];
static FILE* pidfile;
static char *shm; /* shmem pointer */
static int shmid = -1;
static char stext[LENGTH(bricks) * (OUTBUFSIZE + 1 + sizeof(delim))] = {0}; /* status text buffer */
static sigset_t usrsigset; /* sigset for masking interrupts */

//void
//brickexec(unsigned brickndx, unsigned envcount) {
//  pid_t pid;
//  int fds[2];
//  FILE *file;
//  ptrdiff_t *s;
//  size_t n;
//
//  if (pipe(fds) < 0) {
//    elog("pipe() failed:");
//    return;
//  };
//  if ((pid = fork()) < 0)
//    die("fork() failed:");
//  if (pid == 0) {
//    if (envcount) {
//      s = (ptrdiff_t*)shm;
//      do {
//        putenv(shm + *s);
//        s++;
//      } while (*s);
//    }
//    if (close(fds[0]) < 0) { /* child doesn't read */
//      die("close() failed:");
//    }
//    if (dup2(fds[1], 1) < 0) { /* redirect child's stdout to write-end of pipe */
//      die("dup2() failed:");
//    }
//    if (close(fds[1]) < 0) { // do I need this? is this correct
//      die("close() failed:");
//    }
//    if (execvp("sh", (char* const[]){ "sh", "-c", bricks[brickndx].command, NULL }) < 0) {
//      die("execvp() failed:");
//    }
//  } else {
//    if (close(fds[1]) < 0) { /* parent doesn't write */
//      elog("close() failed:");
//      return;
//    }
//    if ((file = fdopen(fds[0], "r")) == NULL) {
//      elog("fdopen() failed:");
//    }
//    /* TODO: Implement timeout mechanism for shell commands.
//     *   As it currently stands fgets() will indefinitely block the daemon
//     *   until at least one line is read of EOF is returned.
//     *   See sigtimedwait().
//     *   Prefix brick output of a timed-out command with [T/O]
//     */
//    waitpid(pid, NULL, 0); /* wait until termination of child */
//    if (fgets(cmdoutbuf[brickndx], OUTBUFSIZE, file) == NULL &&
//        0 != ferror(file)) {
//      elog("fgets() failed:");
//    }
//    n = strlen(cmdoutbuf[brickndx]);
//    if (cmdoutbuf[brickndx][n] == '\n')
//      cmdoutbuf[brickndx][n] = '\0';
//
//    if (fclose(file)) {
//      elog("fclose() failed:");
//    }
//  }
//}

void
cleanup(int exitafter) {

  /* Unlock and delete PID-file */
  fclose(pidfile);
  remove(pidfilepath);

  /* Free shmem */
  if (shmid >= 0) {
    if (shm)
      shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
  }
  if (exitafter)
    exit(0);
}

//void
//usr1(int sig, siginfo_t *si, void *ucontext)
//{
//  const unsigned sigdata = *(unsigned*)(&si->si_value.sival_int);
//  const unsigned envcount = sigdata >> (sizeof(unsigned) * CHAR_BIT - 3);
//  const unsigned brickndx = ((sigdata << 3) >> 3);
//  brickexec(brickndx, envcount);
//  collectflush();
//}
//
//void
//usr2(int sig, siginfo_t *si, void *ucontext)
//{
//  const unsigned sigdata = *(unsigned*)(&si->si_value.sival_int);
//  const unsigned envcount = sigdata >> (sizeof(unsigned) * CHAR_BIT - 3);
//  const unsigned charndx = ((sigdata << 3) >> 3);
//  const int brickndx = brickfromchar(charndx);
//  if (brickndx >= 0) {
//    brickexec(brickndx, envcount);
//    collectflush();
//  }
//}

int
main(int argc, char** argv) {

  key_t key;

  /* Runtime init */
  sprintf(pidfilepath, PIDFILEPATH_TEMPLATE, getuid()); // TODO: error checking
  sigemptyset(&usrsigset); // TODO: error checking
  sigaddset(&usrsigset, SIGUSR1); // TODO: error checking
  sigaddset(&usrsigset, SIGUSR2); // TODO: error checking
  if ((delimlen = utf8strlen(delim)) < 0) {
    elog("delim is not valid UTF-8.");
    exit(1);
  }

  /* Sanity checks */
  if (!access(pidfilepath, F_OK)) {
    elog("PID-file already exists: '%s'", pidfilepath);
    exit(1);
  }

  /* Set up PID-file */
  if (!(pidfile = fopen(pidfilepath, "w"))) {
    elog("Cannot fopen() PID-file '%s':", pidfilepath);
    exit(1);
  }
  fprintf(pidfile, "%u\n", getpid());

  /* shmem init */
  if ((key = ftok(pidfilepath, 1)) < 0) {
    elog("ftok() failed:");
    cleanup(1);
  }
  if ((shmid = shmget(key, shmsz, IPC_CREAT | IPC_EXCL | 0600)) < 0) {
    elog("shmget() failed:");
    cleanup(1);
  }
  if ((shm = (char*)shmat(shmid, NULL, 0)) == (char*)-1) {
    elog("shmat() failed:");
    cleanup(1);
  }

//  /* Run all commands once */
//  for(int ii = 0; ii < LENGTH(bricks); ii++)
//    brickexec(ii, 0);
//  collectflush();

  /* Signal setup */
  signal(SIGQUIT, cleanup);
  signal(SIGTERM, cleanup);
  signal(SIGINT, cleanup);
  signal(SIGSEGV, cleanup);
  signal(SIGHUP, cleanup);
//  struct sigaction sa;
//  sigemptyset(&sa.sa_mask);
//  sa.sa_flags = SA_SIGINFO;
//  sa.sa_sigaction = usr2;
//  sigaction(SIGUSR2, &sa, NULL);
//  sa.sa_sigaction = usr1;
//  sigaction(SIGUSR1, &sa, NULL);

  while(1)
    sleep(1);

  return 0;
}
