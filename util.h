#pragma once

#include <stddef.h>

#define NUMELEM(X) (sizeof(X) / sizeof (X[0]))
#define BETWEEN(X, A, B)        ((A) <= (X) && (X) <= (B))

#define UTF_SIZ 4
long utf8decodebyte(const char c, size_t *i);

void die(const char *fmt, ...);
