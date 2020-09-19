#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <X11/Xlib.h>

typedef struct {
  char* command;
  unsigned long interval;
  char* tag;
} Brick;

#include "config.h"

#define LENGTH(X) (sizeof(X) / sizeof (X[0]))
#define UTF_SIZ 4

static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};

/* Prototypes */
static void brickexec(unsigned, unsigned);
static int brickfromchar(unsigned);
static void cleanup(int);
static void collect(void);
static pid_t daemonpid(void);
static void die(const char *fmt, ...);
static void sigbrick(unsigned, unsigned);
static void sigchar(unsigned, unsigned);
static void tostdout(void);
static void toxroot(void);
static void usr1(int, siginfo_t*, void*);
static void usr2(int, siginfo_t*, void*);
static long utf8decodebyte(const char c, size_t *i);

/* Static variables */
static char cmdoutbuf[LENGTH(bricks)][OUTBUFSIZE + 1] = {0}; /* per-brick stdout buffer */
static char stext[LENGTH(bricks) * (OUTBUFSIZE + 1 + sizeof(delim))] = {0}; /* status text buffer */
static void (*writestatus) () = toxroot; /* dispatcher */
static unsigned delimlen; /* number of utf8 chars in delim */
static char logfile[32];
static char pidfile[32]; /* path to file containing pid */
static sigset_t usrsigset; /* sigset for masking interrupts */

/*
 * Run a brick's command and write its output to its personal buffer.
 * If 'mbutton' is set the envvar 'BUTTON' is introduced prior to execution.
 * Called exclusively by daemon.
 */
void
brickexec(unsigned brickndx, unsigned mbutton) {
  if (mbutton) {
    char num[2] = "0";
    num[0] = mbutton + 48;
    setenv("BUTTON", num, 1);
  }

  FILE *cmdf = popen((bricks + brickndx)->command, "r");
  if (!cmdf)
    die("Opening pipe failed.");
  fgets(cmdoutbuf[brickndx], OUTBUFSIZE + 1, cmdf);
  pclose(cmdf);

  if (mbutton)
    unsetenv("BUTTON");
}

/*
 * Retrieve brick index from UTF-8 character index in status string.
 *
 * Returns -1: charndx belongs to a delimiter
 *         -2: status text contains invalid UTF-8
 *         -3: charndx is out of range
 */
int
brickfromchar(unsigned charndx) {
  size_t charcount, charsize, delimcount;
  char *ptr;

  delimcount = charcount = 0;
  ptr = stext;
  while (*ptr != '\0') {
    if (!strncmp(delim, ptr, sizeof(delim)-1)) {
      charcount += delimlen;
      if (charcount > charndx)
        return -1; /* charndx belongs to a delimiter */
      delimcount++;
      ptr += sizeof(delim) - 1;
    } else {
      if (charcount >= charndx)
        return delimcount;
      utf8decodebyte(*ptr, &charsize);
      if (charsize == 0 || charsize > UTF_SIZ)
        return -2; /* invalid UTF-8 */
      ptr += charsize;
      charcount++;
    }
  }
  return -3; /* charndx out of range */
}

/*
 * Remove PID-file. Called exclusively by daemon.
 */
void
cleanup(int exitafter) {
  if (access(pidfile, F_OK) != -1)
    remove(pidfile);
  if (exitafter)
    exit(0);
}

/*
 * Concatenate bricks' commands outputs and insert delimiters. Masks signals
 * to guarantee atomic write access to stext. Called exclusively by daemon.
 */
void
collect(void) {
  char *p = stext;

  sigprocmask(SIG_BLOCK, &usrsigset, NULL);
  p = stpcpy(p, cmdoutbuf[0]);
  for(int ii = 1; ii < LENGTH(bricks); ii++)
    p = stpcpy(stpcpy(p, delim), cmdoutbuf[ii]);
  sigprocmask(SIG_UNBLOCK, &usrsigset, NULL);
}

/*
 * Retrieve the running daemon's pid from pid-file.
 */
pid_t
daemonpid(void) {
  static char buf[16];
  FILE* file = fopen(pidfile, "r");
  if (!file)
    die("Cannot open PID-file\n");
  if (!fgets(buf, 16, file))
    die("Cannot read from PID-file\n");
  fclose(file);
  pid_t pid = (pid_t)strtoul(buf, NULL, 10);
  return pid;
}

/*
 * Print error and shut down.
 */
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
 * Signal the daemon to execute a brick by its index. Character index and
 * optional mouse button are encoded into the signal's data. Elicits
 * SIGUSR1. Called exclusively by the cli.
 */
void
sigbrick(unsigned brickndx, unsigned mbutton)
{
  union sigval sv = { .sival_int = 0 };
  unsigned *pdata = &sv.sival_int;
  *pdata = (mbutton << (sizeof(unsigned) * CHAR_BIT - 3)) | brickndx;
  sigqueue(daemonpid(), SIGUSR1, sv);
}

/*
 * Signal the daemon to execute brick to which the UTF-8 character at index
 * 'charndx' belongs. Character index and optional mouse button are encoded
 * into the signal's data. Elicits SIGURS2. Called exclusively by the cli.
 */
void
sigchar(unsigned charndx, unsigned mbutton)
{
  union sigval sv = { .sival_int = 0 };
  unsigned *pdata = &sv.sival_int;
  *pdata = (mbutton << (sizeof(unsigned) * CHAR_BIT - 3)) | charndx;
  sigqueue(daemonpid(), SIGUSR2, sv);
}

/*
 * Set x root win name to status text. Called exclusively by daemon.
 */
void
toxroot(void) {
  Display *dpy = XOpenDisplay(NULL);
  XStoreName(dpy, RootWindow(dpy, DefaultScreen(dpy)), stext);
  XCloseDisplay(dpy);
}

/*
 * Print status to stdout. Used for debugging. Called exclusively by daemon.
 */
void
tostdout(void) {
  printf("%s\n", stext);
  fflush(stdout);
}

/*
 * SIGUSR1 handler. Executes brick according to its index. Brick index and
 * mouse button are decoded from the signal's data.
 */
void
usr1(int sig, siginfo_t *si, void *ucontext)
{
  const unsigned sigdata = *(unsigned*)(&si->si_value.sival_int);
  const unsigned mbutton = sigdata >> (sizeof(unsigned) * CHAR_BIT - 3);
  const unsigned brickndx = ((sigdata << 3) >> 3);
  brickexec(brickndx, mbutton);
}

/*
 * SIGUSR2 handler. Executes brick corresponding to utf8 character index
 * in the status text. Character index and mouse button are decoded from the
 * signal's data.
 */
void
usr2(int sig, siginfo_t *si, void *ucontext)
{
  const unsigned sigdata = *(unsigned*)(&si->si_value.sival_int);
  const unsigned mbutton = sigdata >> (sizeof(unsigned) * CHAR_BIT - 3);
  const unsigned charndx = ((sigdata << 3) >> 3);
  const int brickndx = brickfromchar(charndx);
  if (brickndx >= 0)
    brickexec(brickndx, mbutton);
}

/*
 * Decode a single UTF-8 byte.
 *
 * If c is a continuation byte, sets i=0 and returns the value of the data bits.
 * If c is a leading byte, returns the value of the data bits and sets i to the
 * number of bytes in the UTF-8 sequence. Sets i=5 for non-UTF-8 bytes.
 */
long
utf8decodebyte(const char c, size_t *i)
{
  for (*i = 0; *i < (UTF_SIZ + 1); ++(*i))
    if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
      return (unsigned char)c & ~utfmask[*i];
  return 0;
}

int
main(int argc, char** argv) {

  /*
   * Init static variables
   */
  writestatus = toxroot;
  sprintf(logfile, "/tmp/dwmbricks-log-%d", getuid());
  sprintf(pidfile, "/tmp/dwmbricks-pid-%d", getuid());
  sigemptyset(&usrsigset);
  sigaddset(&usrsigset, SIGUSR1);
  sigaddset(&usrsigset, SIGUSR2);
  size_t clen;
  for (const char *ptr = delim; *ptr != '\0'; ptr+=clen) {
    utf8decodebyte(*ptr, &clen);
    if (clen > UTF_SIZ || clen == 0)
      die("Delimiter contains invalid UTF-8.");
    delimlen++;
  }

  /*
   * Parse args
   */
  int mbutton = 0;
  for (int ii = 1; ii < argc; ii++) {
    if (!strcmp(argv[ii], "-m") && argc > ii + 1) {
      mbutton = strtol(argv[ii+1], NULL, 10);
      break;
    }
  }
  for (int ii = 1; ii < argc; ii++) {
    if (!strcmp(argv[ii], "-p")) {
      writestatus = tostdout;
      break;
    } else if (!strcmp(argv[ii], "-c") && argc > ii + 1) {
      sigchar(strtol(argv[ii+1], NULL, 10), mbutton);
      return 0;
    } else if (!strcmp(argv[ii], "-b") && argc > ii + 1) {
      sigbrick(strtol(argv[ii+1], NULL, 10), mbutton);
      return 0;
    } else if (!strcmp(argv[ii], "-t") && argc > ii + 1) {
      for (int jj = 0; jj < LENGTH(bricks); jj++) {
        if (!strcmp(argv[ii+1], bricks[jj].tag))
          sigbrick(jj, mbutton);
      }
      return 0;
    } else {
      die("Invalid arguments.");
    }
  }

  /*
   * Set up PID file
   */
  if (LENGTH(bricks) == 0)
    die("Nothing to do.");
  if (access(pidfile, F_OK) != -1) {
    pid_t pid = daemonpid();
    if (!kill(pid, 0))
      die("Daemon already running.");
    else
      cleanup(1);
  }
  FILE* file = fopen(pidfile, "w");
  if (!file)
    die("Cannot open file pid-file.");
  fprintf(file, "%u\n", getpid());
  fclose(file);

  /*
   * Setup signals
   */
  signal(SIGQUIT, cleanup);
  signal(SIGTERM, cleanup);
  signal(SIGINT, cleanup);
  signal(SIGSEGV, cleanup);
  signal(SIGHUP, cleanup);

  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = usr2;
  sigaction(SIGUSR2, &sa, NULL);
  sa.sa_sigaction = usr1;
  sigaction(SIGUSR1, &sa, NULL);

  /*
   * Run all commands once and enter loop
   */
  for(int ii = 0; ii < LENGTH(bricks); ii++) {
    sigprocmask(SIG_BLOCK, &usrsigset, NULL);
    brickexec(ii, 0);
    sigprocmask(SIG_UNBLOCK, &usrsigset, NULL);
  }

  unsigned long timesec = 0; 
  while (1) {
    sigprocmask(SIG_BLOCK, &usrsigset, NULL);
    collect();
    sigprocmask(SIG_UNBLOCK, &usrsigset, NULL);
    writestatus();
    sleep(1);
    for(int ii = 0; ii < LENGTH(bricks); ii++) {
      if (bricks[ii].interval > 0 && (timesec % bricks[ii].interval == 0)) {
        sigprocmask(SIG_BLOCK, &usrsigset, NULL);
        brickexec(ii, 0);
        sigprocmask(SIG_UNBLOCK, &usrsigset, NULL);
      }
    }
    ++timesec;
  }
}

// TODO(feat): README.md
// TODO(feat): Usage / manpage
// TODO(feat): Allow synchronous startup of daemon for scripting
//   Currently, my boot script executes `dwmbricks &' and later calls
//   `dwmbricks -t ...' at which point it is unclear whether the daemon
//   is ready to take signals or not. Some sort of `dwmbricks --fork'
//   option should be sufficient
