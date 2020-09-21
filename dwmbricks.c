#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <limits.h>
#include <unistd.h>
#include <X11/Xlib.h>

typedef struct {
  char* command;
  unsigned long interval;
  char* tag;
} Brick;

#include "config.h"

/* Macros */
#define LENGTH(X) (sizeof(X) / sizeof (X[0]))

/* Shmem */
static const int shmsz = 4096; /* size of shmem segment in bytes */

/* UTF-8 */
#define UTF_SIZ 4
static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};

/* Prototypes */
static void brickexec(unsigned, unsigned);
static int brickfromchar(unsigned);
static void cleanup(int);
static void collectflush(void);
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
static void (*flushstatus) () = toxroot; /* dispatcher */
static unsigned delimlen; /* number of utf8 chars in delim */
static char logfile[32];
static char pidfile[32]; /* path to file containing pid */
static sigset_t usrsigset; /* sigset for masking interrupts */
static char *shm; /* shmem pointer */
static int shmid = -1;

/*
 * Run a brick's command and write its output to its personal buffer.
 * If 'mbutton' is set the envvar 'BUTTON' is introduced prior to execution.
 * Called exclusively by daemon.
 */
void
brickexec(unsigned brickndx, unsigned envcount) {

  pid_t pid;
  char * dummy[] = { NULL };

  if (envcount) {
    char** const pindex = (char**)shm;
    char* const pdata = (char*)(pindex + envcount + 1);

    /* load index array */
    char **pi = pindex;
    char *pd = pdata;
    *pi = pd;
    ++pi;
    while (*pi) {
      pd += strlen(pd) + 1; // TODO: Encode in pindex instead of using strlen
      *pi = pd;
      ++pi;
    }

    pi = pindex;
    while (*pi) {
      printf("env = %s\n", *pi);
      pi++;
    }
  }

  if ((pid = fork()) < 0)
    die("fork() failed:");
  if (pid == 0) {
    if (envcount > 0) {
      char * envs[] = { "dummy", NULL };
      execvpe(bricks[brickndx].command, envs, (char**)shm);
    }
    else
      execvp(bricks[brickndx].command, dummy);
    exit(0);
  } else {
    printf("parent!\n");

  }

//    int corpse;
//    int status;
//    while ((corpse = wait(&status)) > 0 && corpse != pid)
//        printf("Parent: child %d died with status 0x%.4X\n", corpse, status);
//    char line[4096];
//    if (fgets(line, sizeof(line), stdin) == 0)
//    {
//        fprintf(stderr, "Failed to read line in parent\n");
//        exit(4);
//    }
//    while (fgets(line, sizeof(line), stdin) != 0)
//    {
//        line[strcspn(line, "\n")] = '\0';
//        printf("Parent: read [%s]\n", line);
//    }
//  }
//
//  FILE *cmdf = popen((bricks + brickndx)->command, "r");
//  if (!cmdf)
//    die("Opening pipe failed.");
//  fgets(cmdoutbuf[brickndx], OUTBUFSIZE + 1, cmdf);
//  pclose(cmdf);
//
//  if (mbutton)
//    unsetenv("BUTTON");
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
  if (shmid >= 0) {
    if (shm)
      shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
  }
  if (access(pidfile, F_OK) != -1)
    remove(pidfile);
  if (exitafter)
    exit(0);
}

/*
 * Concatenate bricks' command outputs inserting delimiters and write out
 * status text. Called exclusively by daemon.
 */
void
collectflush(void) {
  char *p = stext;

  p = stpcpy(p, cmdoutbuf[0]);
  for(int ii = 1; ii < LENGTH(bricks); ii++)
    p = stpcpy(stpcpy(p, delim), cmdoutbuf[ii]);
  flushstatus();
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
 * TODO: adjust doc
 * Signal the daemon to execute a brick by its index. Character index and
 * optional mouse button are encoded into the signal's data. Elicits
 * SIGUSR1. Called exclusively by the cli.
 */
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

/*
 * Write pid into pidfile.
 */
void
storedaemonpid(pid_t pid)
{
  FILE* file;
  if (!(file = fopen(pidfile, "w")))
    die("fopen() failed:");
  fprintf(file, "%u\n", pid);
  if (fclose(file))
    die("fclose() failed:");
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
 * TODO: adjust doc
 */
void
usr1(int sig, siginfo_t *si, void *ucontext)
{
  const unsigned sigdata = *(unsigned*)(&si->si_value.sival_int);
  const unsigned envcount = sigdata >> (sizeof(unsigned) * CHAR_BIT - 3);
  const unsigned brickndx = ((sigdata << 3) >> 3);
  // load env
  brickexec(brickndx, envcount);
  collectflush();
  // unloadenv
}

/*
 * SIGUSR2 handler. Executes brick corresponding to utf8 character index
 * in the status text. Character index and mouse button are decoded from the
 * signal's data.
 * TODO: adjust doc
 */
void
usr2(int sig, siginfo_t *si, void *ucontext)
{
  const unsigned sigdata = *(unsigned*)(&si->si_value.sival_int);
  const unsigned envcount = sigdata >> (sizeof(unsigned) * CHAR_BIT - 3);
  const unsigned charndx = ((sigdata << 3) >> 3);
  const int brickndx = brickfromchar(charndx);
  if (brickndx >= 0)
    brickexec(brickndx, envcount);
  collectflush();
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
  key_t key;

  /* Runtime init */
  flushstatus = toxroot;
  sprintf(logfile, "/tmp/dwmbricks-log-%d", getuid()); // TODO: ensure maxlen (snprintf)
  sprintf(pidfile, "/tmp/dwmbricks-pid-%d", getuid()); // TODO: see above
  sigemptyset(&usrsigset);
  sigaddset(&usrsigset, SIGUSR1);
  sigaddset(&usrsigset, SIGUSR2);
  size_t charsize;
  for (const char *ptr = delim; *ptr != '\0'; ptr+=charsize) {
    utf8decodebyte(*ptr, &charsize);
    if (charsize > UTF_SIZ || charsize == 0)
      die("Delimiter contains invalid UTF-8.");
    delimlen++;
  }

  /* Parse args */
  int mbutton = 0;
  int dofork = 0;
  unsigned envcount;
  switch (argc) {
  case 1: /* daemon */
    break;
  case 2: /* daemon */
    if (!strcmp(argv[1], "-f"))
      dofork = 1;
    else if (!strcmp(argv[1], "-p"))
      flushstatus = tostdout;
    else
      die("Invalid arguments.");
    break;
  default: { /* cli */
    if ((envcount = (argc - 3) / 2) > 0) {
      /* shmem */
      if ((key = ftok(pidfile, 1)) < 0)
        die("ftok() failed:");
      if ((shmid = shmget(key, shmsz, 0)) < 0)
        die("shmget() failed:");
      if ((shm = (char*)shmat(shmid, NULL, 0)) == (char*)-1)
        die("shmat() failed:");

      /* paste envvar strings into shmem */
      char **pindex = (char**)shm;
      const char *databegin = (char*)(pindex + envcount + 1);
      char* pdata = databegin;
      for (int ii = 0; ii < envcount; ++ii) {
        pdata += sprintf(pdata, "%s", argv[4+ii*2]) + 1;
        *pindex = databegin - pdata; // store offset (address useless for shmem)
        pindex++;
      }
      *((char**)shm + envcount) = (char*)NULL;
    }

    unsigned ndx;
    if (!strcmp(argv[1], "-c")) {
      ndx = strtoul(argv[2], NULL, 10);
      sigchar(ndx, envcount);
    } else if (!strcmp(argv[1], "-t")) {
      for (int ii = 0; ii < LENGTH(bricks); ii++) {
        if (!strcmp(argv[2], bricks[ii].tag)) {
          sigbrick(ii, envcount);
        }
      }
    } else if (!strcmp(argv[1], "-b")) {
      ndx = strtoul(argv[2], NULL, 10);
      sigbrick(ndx, envcount);
    } else
      die("Invalid arguments.");
    return 0;
    break;
  }
  }

  /* Sanity checks */
  if (LENGTH(bricks) == 0)
    die("Nothing to do.");
  if (access(pidfile, F_OK) != -1) {
    pid_t pid = daemonpid();
    if (!kill(pid, 0))
      die("Daemon already running.");
    else
      cleanup(0); /* remove stale pid file */
  }

  /* Fork and wait for daemon startup to finish */
  pid_t pid;
  if (dofork) {
    if ((pid = fork()) < 0)
      die("fork() failed:");
    if (pid > 0) {
      storedaemonpid(pid);
      sigset_t parentsigset;
      int sig;
      sigemptyset(&parentsigset);
      sigaddset(&parentsigset, SIGQUIT);
      sigwait(&parentsigset, &sig);
      return 0;
    }
  } else
    storedaemonpid(getpid());

  /* shmem setup (daemon reader, cli writer) */
  if ((key = ftok(pidfile, 1)) < 0)
    die("ftok() failed:");
  if ((shmid = shmget(key, shmsz, IPC_CREAT | IPC_EXCL | 0600)) < 0)
    die("shmget() failed:");
  if ((shm = (char*)shmat(shmid, NULL, 0)) == (char*)-1)
    die("shmat() failed:");

  /* Run all commands once */
  for(int ii = 0; ii < LENGTH(bricks); ii++)
    brickexec(ii, 0);
  collectflush();

  /* Set up signals */
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

  /* Signal to parent that daemon (child) is ready */
  if (dofork)
    kill(getppid(), SIGQUIT);

  /* Superloop */
  unsigned long timesec = 0; 
  int update = 1;
  while (1) {
    if (update) {
      sigprocmask(SIG_BLOCK, &usrsigset, NULL);
      collectflush();
      sigprocmask(SIG_UNBLOCK, &usrsigset, NULL);
      update = 0;
    }
    sleep(1);
    for(int ii = 0; ii < LENGTH(bricks); ii++) {
      if (bricks[ii].interval > 0 && (timesec % bricks[ii].interval == 0)) {
        sigprocmask(SIG_BLOCK, &usrsigset, NULL);
        brickexec(ii, 0);
        sigprocmask(SIG_UNBLOCK, &usrsigset, NULL);
        update = 1;
      }
    }
    ++timesec;
  }
}

// TODO(feat): Improve cli args handling
//   daemon arguments are not separated from cli arguments and nonsense like
//   $0 -f -t keymap is possible
// TODO(feat): Semver
// TODO(feat): README.md
// TODO(feat): Usage / manpage
// TODO(feat): Makefile best practices
// TODO(future): Replace -m cli parameter with generic passing-on of envvars
//   E.g. $0 -t "keymap" -e "BUTTON=3" -e "FLAG=FOO" ..
//   This requires an additional communication channel between the cli and daemon
//   to pass the environment strings through.
//   Idea: Shared memory segment (single writer (cli)/single reader (daemon)).
//         -e adds a flag to the signal's payload indicating that the shared
//         memory segment shall be parsed for envvars.
//         - Lock smem
//         - Store old envval
//         - load new envval from smem
//         - Unlock smem
//         - execbrick
//         - restore old envval
