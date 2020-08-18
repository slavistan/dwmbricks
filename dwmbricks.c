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
#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))

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
/* Number of utf8-chars in delim (not counting '\0') */
static int numcdelim;

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

#define UTF_SIZ 4
#define UTF_INVALID 0xFFFD

static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const long utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static const long utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

/*
 * Decode a single UTF-8 byte.
 *
 * If c is a continuation byte, sets i=0 and returns the value of the data bits.
 * If c is a leading byte, returns the value of the data bits and sets i to the
 * number of bytes in the UTF-8 sequence. Sets i=5 for non-UTF-8 bytes.
 */
static long
utf8decodebyte(const char c, size_t *i)
{
	for (*i = 0; *i < (UTF_SIZ + 1); ++(*i))
		if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
			return (unsigned char)c & ~utfmask[*i];
	return 0;
}

static size_t
utf8validate(long *u, size_t i)
{
	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; ++i)
		;
	return i;
}

size_t
utf8decode(const char *c, long *u, size_t clen)
{
	size_t i, j, len, type;
	long udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len);
	if (!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
		if (type)
			return j;
	}
	if (j < len)
		return 0;
	*u = udecoded;
	utf8validate(u, len);

	return len;
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

  /* TODO: Encode brickid into signal payload and remove RT signals. */

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

/*
 //... asd
 */
void
daemonkickbyname(const char* name) {
  for (int ii = 0; ii < NUMELEM(bricks); ++ii) {
    if (!strcmp(name, (bricks + ii)->name)) {
      daemonkickbyindex(ii);
    }
  }
}

void
daemonkickbyutf8charindex(unsigned int index /* 0-indexed */) {
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
  sigqueue(daemonpid(), SIGUSR1, sv);
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
      printf("%u\n", daemonpid());
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
        else if (!strcmp("--utf8index", argv[2])) {
          long idx = strtol(argv[3], NULL, 10);
          if (idx < 0)
            die("Index must be ≥ 0.");
          daemonkickbyutf8charindex((unsigned int)idx);
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
  long u;
  for (int ii = 0; delim[ii] != '\0'; numcdelim++) {
    ii += utf8decode(delim + ii, &u, 6);
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
//
// TODO(maybe): Optimize UTF-8 implementation.
//
//     utf8...(..) copied from dwm source. Might be overkill as I just need
//     to extract the lengths of valid UTF-8 byte sequences.
