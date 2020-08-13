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

#define NUMELEM(X) (sizeof(X) / sizeof (X[0]))

typedef struct {
  char* command;
  unsigned int interval;
  unsigned int signal;
  char* name;
} Brick;

#include "config.h"

void cleanup(bool);
void collect(void);
pid_t daemonpid(void);
void daemonkickbyindex(unsigned int);
void daemonkickbysignal(pid_t usersigno);
void die(const char *fmt, ...);
void setroot(void);
void sighandler(int signum);

static Display *dpy;
static int screen;
static Window root;
/* Array of buffers to store commands' outputs */
static char cmdoutbuf[NUMELEM(bricks)][OUTBUFSIZE + 1] = {0};
/* Buffer containing the fully concatenated status string */
static char statusbuf[NUMELEM(bricks) * (OUTBUFSIZE + 1 + sizeof(delim))] = {0};
/* Dispatcher function */
static void (*writestatus) () = setroot;

void
die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
    fputc(' ', stderr);
    perror(NULL);
  } else {
    fputc('\n', stderr);
  }
  exit(1);
}

/*
 * Retrieve the running daemon's pid
 */
pid_t daemonpid(void) {
  FILE* pidfile = fopen(PIDFILEPATH, "r");
  if (!pidfile)
    die("Cannot open PID-file:");
  int buflen = 16;
  char buf[buflen];
  if (!fgets(buf, buflen, pidfile))
    die("Cannot read from PID-file:");
  return (pid_t)strtoul(buf, NULL, 10);
}

/*
 * Trigger execution of a brick's command in the running daemon.
 * Sends the according brick's signal to the daemon.
 */
void
daemonkickbyindex(unsigned int brickid) {
  assert(brickid < NUMELEM(bricks));

  daemonkickbysignal((bricks + brickid)->signal);
}

/*
 * Send user signal to daemon.
 */
void
daemonkickbysignal(pid_t usersigno) {
  assert(0 <= usersigno && usersigno <= (SIGRTMAX - SIGRTMIN));

  kill(daemonpid(), SIGRTMIN + usersigno);
}

void
daemonkickbyname(const char* name) {
  for (int ii = 0; ii < NUMELEM(bricks); ++ii) {
    if (!strcmp(name, (bricks + ii)->name)) {
      daemonkickbyindex(ii);
    }
  }
}

/*
 * Terminate daemon.
 */
void daemonkill(void) {
  kill(daemonpid(), SIGTERM);
}

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
  statusbuf[0] = '\0';
  for(int ii = 0; ii < NUMELEM(bricks); ii++) {
    strcat(statusbuf, cmdoutbuf[ii]);
    if (ii < NUMELEM(bricks) - 1) {
      strcat(statusbuf, delim);
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
  XStoreName(dpy, root, statusbuf);
  XCloseDisplay(dpy);
}

/*
 * Print status to stdout
 */
void
pstdout(void) {
  printf("%s\n", statusbuf);
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

int
main(int argc, char** argv) {
  /* Parse args */
  if (argc > 1) {
    if (!strcmp("-p", argv[1])) {
      writestatus = pstdout;
    }
    else if (!strcmp("pid", argv[1])) {
      printf("%u\n", daemonpid());
      exit(0);
    }
    else if (!strcmp("kick", argv[1])) {
      if (argc == 4) {
        if (!strcmp("--index", argv[2]))
          daemonkickbyindex(strtoul(argv[3], NULL, 10));
        else if (!strcmp("--signal", argv[2]))
          daemonkickbysignal(strtoul(argv[3], NULL, 10));
        else if (!strcmp("--name", argv[2]))
          daemonkickbyname(argv[3]);
      }
      else if (argc == 3)
        daemonkickbyname(argv[2]);
      else
        die("Invalid arguments.");
      exit(0);
    }
    else if (argc == 3 && !strcmp("--daemonkickbysignal", argv[1])) {
      unsigned int sig = strtoul(argv[2], NULL, 10);
      daemonkickbysignal(sig);
      exit(0);
    } else if (argc == 3 && !strcmp("--daemonkickbyname", argv[1])) {
      daemonkickbyname(argv[2]);
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

  /* Create pid file */
  if (access(PIDFILEPATH, F_OK) != -1) {
    pid_t pid = daemonpid();
    if (!kill(pid, 0))
      die("Daemon already running.");
    else
      cleanup(false);
  }
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
      if ((bricks + ii)->interval > 0 && timesec % (bricks + ii)->interval == 0)
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
