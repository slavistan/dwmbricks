/**
 * This file contains code use by cli and daemon.
 */
#include <stddef.h>
#include <sys/types.h>

typedef struct {
  char* command;
  unsigned long interval;
  char* tag;
} Instruction;

/* Macros */
#define LENGTH(X) (sizeof(X) / sizeof (X[0]))

ssize_t utf8strlen(const char *p);
ssize_t utf8charsz(char c);
void elog(const char *fmt, ...);
