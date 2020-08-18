#pragma once

typedef struct {
  char* command;
  unsigned int interval;
  unsigned int signal; // TODO: remove
  char* tag;
} Brick;

extern const Brick bricks[];
extern unsigned int numbricks;
extern const char PIDFILEPATH[];
