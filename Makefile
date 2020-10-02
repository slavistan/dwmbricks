.POSIX:

PREFIX = /usr/local
CC = gcc

all: dwmbricks.c config.h Makefile
	$(CC) dwmbricks.c -O2 -lX11 -o dwmbricks
clean:
	rm -f *.o dwmbricks
install: dwmbricks
	mkdir -p $(PREFIX)/bin
	cp -f dwmbricks $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/dwmbricks
uninstall:
	rm -f $(PREFIX)/bin/dwmbricks

.PHONY: clean uninstall
