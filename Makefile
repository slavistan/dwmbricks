.POSIX:

PREFIX = /usr/local
CC = gcc

utils: utils.c utils.h
	$(CC) -O2 -c -o utils.o utils.c
cli: cli.c utils
	$(CC) -O2 utils.o -o cli cli.c
daemon: daemon.c utils
	$(CC) -O2 utils.o -o daemon daemon.c
install: cli daemon
	mkdir -p $(PREFIX)/bin
	cp -f dwmbricks $(PREFIX)/bin
uninstall:
	
clean:
	rm -f *.o dwmbricks

.PHONY: clean uninstall
