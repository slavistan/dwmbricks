#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "config.h"

static void cleanup(int);
static void sigbrick(unsigned, unsigned);
static void sigchar(unsigned, unsigned);
static char clilockfile[32]; /* path to cli lock file */

int
main(int benis, char** argv) {

  printf("is this the real life?\n");

  return 0;
}
