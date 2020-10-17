/**
 * This file contains code use by cli and daemon.
 */
#include <stddef.h>

typedef struct {
  char* command;
  unsigned long interval;
  char* tag;
} Brick;

/* Macros */
#define LENGTH(X) (sizeof(X) / sizeof (X[0]))

size_t utf8strlen(const char *p);
void elog(const char *fmt, ...);
