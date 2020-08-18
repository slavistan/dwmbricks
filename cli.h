#pragma once

#include <signal.h>

pid_t getdaemonpid(void);
void daemonkill(void);
void daemonkickbysignal(pid_t usersigno);
void daemonkickbyname(const char* name);
void daemonkickbycharindex(unsigned int index /* 0-indexed */);
void daemonkickbyindex(unsigned int index /* 0-indexed */);
