#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <limits.h>
#include <unistd.h>
#include <X11/Xlib.h>

#include "dwmbricks.h"
#include "util.h"
#include "cli.h"

#include "config.h"

static void cleanup(int);
static void collect(void);
static void setroot(void);
static void sighandler(int signum);

static Display *dpy;
static int screen;
static Window root;
/* Array of buffers to store commands' outputs */
static char cmdoutbuf[LENGTH(bricks)][OUTBUFSIZE + 1] = {0};
/* Buffer containing the fully concatenated status string */
static char stext[LENGTH(bricks) * (OUTBUFSIZE + 1 + sizeof(delim))] = {0};
/* Dispatcher function */
static void (*writestatus) () = setroot;
/* Number of utf8-chars in delim (not counting '\0') */
static unsigned int numcdelim;
unsigned int numbricks = LENGTH(bricks);


/*
 * Remove PID-file.
 */
void
cleanup(int exitafter) {
  if (access(PIDFILEPATH, F_OK) != -1)
    remove(PIDFILEPATH);
  if (exitafter)
    exit(0);
}

/*
 * Run a brick's command and write its output to the associated buffer.
 */
void
runbrickcmd(unsigned int brickid) {
  assert(brickid < LENGTH(bricks));

  printf("brick cmd %u\n", brickid);

  FILE *cmdf = popen((bricks + brickid)->command, "r");
  if (!cmdf)
    die("Opening pipe failed.");
  fgets(cmdoutbuf[brickid], OUTBUFSIZE + 1, cmdf);
  pclose(cmdf);
}

/*
 * Concatenate command outputs and insert delimiters.
 */
void
collect(void) {
  stext[0] = '\0';
  for(int ii = 0; ii < LENGTH(bricks); ii++) {
    strcat(stext, cmdoutbuf[ii]);
    if (ii < LENGTH(bricks) - 1) {
      strcat(stext, delim);
    }
  }
}

/*
 * Set x root win name to status
 */
void
setroot(void) {
  Display *d = XOpenDisplay(NULL);
  if (d)
    dpy = d;
  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);
  XStoreName(dpy, root, stext);
  XCloseDisplay(dpy);
}

/*
 * Print status to stdout
 */
void
pstdout(void) {
  printf("%s\n", stext);
  fflush(stdout);
}

/*
 * General signal handler
 */
void
sighandler(int signo) {
  switch (signo) {
    case SIGUSR1:
      infof("sigusr1\n");
      break;

    case SIGUSR2:
      infof("sigurs2\n");
      break;
  }
}

/*
 * Signal the daemon to execute a brick by its index. Brick index is encoded
 * into the signal's data.
 */
static void
sigbrick(unsigned brickndx, unsigned mbutton)
{
  union sigval sv = { .sival_int = 0 };
  unsigned *pdata = &sv.sival_int;
  *pdata = (mbutton << (sizeof(unsigned) * CHAR_BIT - 3)) | brickndx;
  sigqueue(getdaemonpid(), SIGUSR1, sv);
}

/*
 * Retrieve bricks by tag encoded as a bitmask.
 * TODO: Explicitly state the max num of bricks of 64.
 */
// static unsigned long long
// tagmask(const char* tag)
// {
//   unsigned long long mask = 0;
//   for (unsigned ii = 0; ii < LENGTH(bricks); ++ii) {
//     if (!strcmp(bricks[ii].tag, tag))
//       mask |= (1 << (unsigned long long)ii);
//   }
//   return mask;
// }


/*
 * Signal the daemon to execute bricks whose tags match.
 */
static void
sigtag(const char *tag, unsigned mbutton)
{
  unsigned long long mask = 0;
  for (unsigned ii = 0; ii < LENGTH(bricks); ++ii) {
    if (!strcmp(bricks[ii].tag, tag))
      mask |= (1 << (unsigned long long)ii);
  }
  for (unsigned ii = 0; mask; ii++) {
    if (mask & (1 << ii)) {
      sigbrick(ii, mbutton);
    }
  }
}

/*
 * Signal the daemon to execute brick to which the UTF-8 character index
 * belongs.
 */
static void
sigchar(unsigned charndx, unsigned mbutton)
{
  union sigval sv = { .sival_int = 0 };
  unsigned *pdata = &sv.sival_int;
  *pdata = (mbutton << (sizeof(unsigned) * CHAR_BIT - 3)) | charndx;
  sigqueue(getdaemonpid(), SIGUSR2, sv);
}

/*
 * Retrieve brick index from UTF-8 character index in status
 *
 * Returns -1 iff cindex belongs to a delimiter
 *         -2 if  status text contains invalid UTF-8
 *         -3 iff cindex is out of range
 */
static int
brickfromcharindex(unsigned int charndx) {
  size_t ccount, clen, delimcount;
  char *ptr;

  delimcount = ccount = 0;
  ptr = stext;
  while (*ptr != '\0') {
    if (!strncmp(delim, ptr, sizeof(delim)-1)) {
      ccount += numcdelim;
      if (ccount > charndx)
        return -1; // cindex belongs to a delimiter
      delimcount++;
      ptr += sizeof(delim) - 1;
    } else {
      if (ccount >= charndx)
        return delimcount;
      utf8decodebyte(*ptr, &clen);
      if (clen == 0 || clen > UTF_SIZ)
        return -2; // invalid UTF-8
      ptr += clen;
      ccount++;
    }
  }
  return -3; // cindex out of range
}

static void
charhandler(int sig, siginfo_t *si, void *ucontext)
{
  const unsigned data = *(unsigned*)(&si->si_value.sival_int);
  const unsigned mbutton = data >> (sizeof(unsigned) * CHAR_BIT - 3);
  const unsigned cindex = ((data << 3) >> 3);

  const int bindex = brickfromcharindex(cindex);
  if (bindex >= 0) {
    char envs[2] = "0";
    envs[0] = (char)(mbutton + 48);
    setenv("BUTTON", envs, 1);
    runbrickcmd(bindex);
    unsetenv("BUTTON");
  }
}

int
main(int argc, char** argv) {
  int mbutton = 0;
  for (int ii = 1; ii < argc; ii++) {
    if (!strcmp(argv[ii], "-m") && argc > ii) {
      mbutton = strtol(argv[ii+1], NULL, 10);
      break;
    }
  }
  for (int ii = 1; ii < argc; ii++) {
    if (!strcmp(argv[ii], "-p")) {
      writestatus = pstdout;
      break;
    } else if (!strcmp(argv[ii], "-c") && argc > ii) {
      sigchar(strtol(argv[ii+1], NULL, 10), mbutton);
      exit(0);
    } else if (!strcmp(argv[ii], "-b") && argc > ii) {
      sigbrick(strtol(argv[ii+1], NULL, 10), mbutton);
      exit(0);
    } else if (!strcmp(argv[ii], "-t") && argc > ii) {
      sigtag(argv[ii+1], mbutton);
      exit(0);
    } else {
      die("Invalid arguments.");
    }
  }

  /* Setup signals */
  signal(SIGQUIT, cleanup);
  signal(SIGTERM, cleanup);
  signal(SIGINT, cleanup);
  signal(SIGSEGV, cleanup);
  signal(SIGHUP, cleanup);

  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = charhandler;
  sa.sa_flags = SA_SIGINFO; /* Important. */
  sigaction(SIGUSR2, &sa, NULL);

  /* Misc setup */
  size_t clen;
  for (char *ptr = delim; *ptr != '\0'; ptr+=clen) {
    utf8decodebyte(*ptr, &clen);
    if (clen > UTF_SIZ || clen == 0)
      die("Delimiter contains invalid UTF-8.");
    numcdelim += clen;
  }

  /* Create pid file */
  if (access(PIDFILEPATH, F_OK) != -1) {
    pid_t pid = getdaemonpid();
    if (!kill(pid, 0))
      die("Daemon already running.");
    else
      cleanup(1);
  }
  FILE* pidfile = fopen(PIDFILEPATH, "w");
  if (!pidfile)
    die("Cannot open file\n");
  fprintf(pidfile, "%u\n", getpid());
  fclose(pidfile);

  /* Run all commands once */
  for(int ii = 0; ii < LENGTH(bricks); ii++) {
    runbrickcmd(ii);
  }

  /* Enter loop */
  unsigned int timesec = 0;
  while (1) {
    collect();
    writestatus();
    sleep(1);
    for(int ii = 0; ii < LENGTH(bricks); ii++) {
      if ((bricks + ii)->interval > 0 && (timesec % (bricks + ii)->interval) == 0)
        runbrickcmd(ii);
    }
    ++timesec;
  }
}

// TODO(fix): Eliminate race conditions
//
//     Signals may cause a problems for
//       1. collect(), as it reads from the command buffers (inconsistent data)
//       2. runbrickcmd() outside the signal handlers (race condition)
//     Block signals before execution.
//
// TODO(maybe): Implement proper real-time handling. (possible yagni)
//
//     The current mechanism to implement periodical execution increments a
//     counter of seconds sleeps for 1 second. Thus incoming signals skew the
//     time tracking as they interrupt the sleep and start the next second's
//     cycle early.
//
// TODO(maybe): Use portable signal handling
//
// TODO(maybe): Multithreaded brick execution
//
//     The single-threaded nature of this requires that execution times of
//     brick commands be minimal. This poses a problem for bricks whose
//     commands take longer to execute (e.g. curling any remote service).
//
//  TODO(fix): Check correct usage of integral types.
