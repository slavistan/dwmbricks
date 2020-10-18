/**
 * This file contains code use by cli and daemon.
 */
#include <stddef.h>

typedef struct {
  char* command;
  unsigned long interval;
  char* tag;
} Instruction;

/* Macros */
#define LENGTH(X) (sizeof(X) / sizeof (X[0]))

#define UTF_SIZ 4
size_t utf8strlen(const char *p);
long utf8decodebyte(const char c, size_t *i);
void elog(const char *fmt, ...);
