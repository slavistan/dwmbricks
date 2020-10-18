#include "utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UTF_SIZ 4
static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};

static long utf8decodebyte(const char c, size_t *i);

/*
 * Returns size of UTF-8 character from start byte 'c'. Returns -1 if 'c' is not
 * a valid UTF-8 start byte.
 */
ssize_t
utf8charsz(char c) {
  size_t charsz;

  utf8decodebyte(c, &charsz);
  if (charsz == 0 || charsz > UTF_SIZ)
    return -1;
  return charsz;
}

/*
 * Returns length of a null-terminated UTF-8 string pointed by 'p'. If invalid
 * bytes are encountered -1 is returned instead.
 */
ssize_t
utf8strlen(const char *p) {
  size_t charsz, len;

  for (len = 0; *p != '\0'; p+=charsz) {
    utf8decodebyte(*p, &charsz);
    if (charsz > UTF_SIZ || charsz == 0) {
      return (size_t)-1;
    }
    len++;
  }
  return len;
}

long
utf8decodebyte(const char c, size_t *i) // TODO: cli doesn't need that, does it?
{
  for (*i = 0; *i < (UTF_SIZ + 1); ++(*i))
    if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
      return (unsigned char)c & ~utfmask[*i];
  return 0;
}

/*
 * Log message to stderr
 */
void
elog(const char *fmt, ...) {
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
}
