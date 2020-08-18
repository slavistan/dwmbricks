#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <X11/Xlib.h>

#include "dwmbricks.h"
#include "util.h"
#include "cli.h"

#include "config.h"

static void cleanup(bool);
static void collect(void);
static void setroot(void);
static void sighandler(int signum);

static Display *dpy;
static int screen;
static Window root;
/* Array of buffers to store commands' outputs */
static char cmdoutbuf[NUMELEM(bricks)][OUTBUFSIZE + 1] = {0};
/* Buffer containing the fully concatenated status string */
static char stext[NUMELEM(bricks) * (OUTBUFSIZE + 1 + sizeof(delim))] = {0};
/* Dispatcher function */
static void (*writestatus) () = setroot;
/* Number of utf8-chars in delim (not counting '\0') */
static unsigned int numcdelim;
unsigned int numbricks = NUMELEM(bricks);


/*
 * Remove PID-file.
 */
void
cleanup(bool exitafter) {
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
  assert(brickid < NUMELEM(bricks));

  printf("brick cmd %u\n", brickid);

  FILE *cmdf = popen((bricks + brickid)->command, "r");
  if (!cmdf)
    die("Opening pipe failed.");
  fgets(cmdoutbuf[brickid], OUTBUFSIZE + 1, cmdf);
  pclose(cmdf);
}

/*
 * Concatenates command outputs and inserts delimiters
 */
void
collect(void) {
  stext[0] = '\0';
  for(int ii = 0; ii < NUMELEM(bricks); ii++) {
    strcat(stext, cmdoutbuf[ii]);
    if (ii < NUMELEM(bricks) - 1) {
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
  /* User RT-signals */
  if (signo >= SIGRTMIN && signo <= SIGRTMAX) {
    for (int ii = 0; ii < NUMELEM(bricks); ++ii) {
      if ((bricks + ii)->signal + SIGRTMIN == signo)
        runbrickcmd(ii);
    }
    return;
  }

  /* Normal signals */
  switch (signo) {
    case SIGQUIT:
    case SIGTERM:
    case SIGINT:
    case SIGSEGV:
    case SIGHUP:
      cleanup(true); break;
  }
}

/*
 * Retrieve bricks by tag encoded as bitmask.
 */
static unsigned long long
brickfromtag(const char* tag)
{
  unsigned long long mask = 0;
  for (unsigned int ii = 0; ii < NUMELEM(bricks); ++ii) {
    if (!strcmp(bricks[ii].tag, tag))
      mask |= (1 << (unsigned long long)ii);
  }
  return mask;
}

/*
 * Retrieve brick index from UTF-8 character index in status
 *
 * Returns -1 iff cindex belongs to a delimiter
 *         -2 if  status text contains invalid UTF-8
 *         -3 iff cindex is out of range
 */
static
int brickfromcharindex(unsigned int cindex) {
  size_t ccount, clen, delimcount;
  char *ptr;

  delimcount = ccount = 0;
  ptr = stext;
  while (*ptr != '\0') {
    if (!strncmp(delim, ptr, sizeof(delim)-1)) {
      ccount += numcdelim;
      if (ccount > cindex)
        return -1; // cindex belongs to a delimiter
      delimcount++;
      ptr += sizeof(delim) - 1;
    } else {
      if (ccount >= cindex)
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

// utf8index handler
static void
sig_charidx(int sig, siginfo_t *si, void *ucontext)
{
  static char envs[16];
  const int payload = si->si_value.sival_int;

  /* Extract cindex & mbutton from signal data */
  unsigned int cindex = payload & (0xFF);
  unsigned int mbutton = (payload & (0xFF00)) >> 8;
  if (cindex < 0)
    return;

  int bindex = brickfromcharindex(cindex);
  if (bindex >= 0) {
    sprintf(envs, "BUTTON=%u", mbutton);
    putenv(envs);
    runbrickcmd(bindex);
    unsetenv("BUTTON");
  }
}

int
main(int argc, char** argv) {
  /* Parse args */
  if (argc > 1) {
    if (!strcmp("-p", argv[1])) {
      writestatus = pstdout;
    }
    else if (!strcmp("pid", argv[1])) {
      printf("%u\n", getpid());
      exit(0);
    }
    else if (!strcmp("kill", argv[1])) {
      daemonkill();
      exit(0);
    }
    else if (!strcmp("kick", argv[1])) {
      if (argc == 4) {
        if (!strcmp("--index", argv[2]))
          daemonkickbyindex(strtoul(argv[3], NULL, 10));
        else if (!strcmp("--signal", argv[2]))
          daemonkickbysignal(strtoul(argv[3], NULL, 10));
        else if (!strcmp("--charindex", argv[2])) {
          long idx = strtol(argv[3], NULL, 10);
          if (idx < 0)
            die("Index must be ≥ 0.");
          daemonkickbycharindex((unsigned int)idx);
        }
        else if (!strcmp("--name", argv[2]))
          daemonkickbyname(argv[3]);
        else
          die("Invalid arguments.");
      }
      else
        die("Invalid arguments.");
      exit(0);
    } else {
      die("Invalid arguments.");
    }
  }

  /* Setup signals */
  signal(SIGQUIT, sighandler);
  signal(SIGTERM, sighandler);
  signal(SIGINT, sighandler);
  signal(SIGSEGV, sighandler);
  signal(SIGHUP, sighandler);
  for (int ii = 0; ii < NUMELEM(bricks); ++ii) {
    unsigned int sig = SIGRTMIN + (bricks + ii)->signal;
    if (signal(sig, sighandler) == SIG_ERR)
      die("Cannot register user signal %u:", (bricks + ii)->signal);
  }
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = sig_charidx;
  sa.sa_flags = SA_SIGINFO; /* Important. */
  sigaction(SIGUSR1, &sa, NULL);

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
      cleanup(false);
  }
  printf("dingo!\n");
  FILE* pidfile = fopen(PIDFILEPATH, "w");
  if (!pidfile)
    die("Cannot open file\n");
  fprintf(pidfile, "%u\n", getpid());
  fclose(pidfile);

  /* Run all commands once */
  for(int ii = 0; ii < NUMELEM(bricks); ii++) {
    runbrickcmd(ii);
  }

  /* Enter loop */
  unsigned int timesec = 0;
  while (true) {
    collect();
    writestatus();
    sleep(1);
    for(int ii = 0; ii < NUMELEM(bricks); ii++) {
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
