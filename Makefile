.POSIX:

PREFIX = /usr/local
CC = gcc

all: staccatod staccato
utils: utils.c utils.h
	$(CC) -O2 -c -o utils.o utils.c
staccato: staccato.c utils
	$(CC) -O2 utils.o -o staccato staccato.c
staccatod: staccatod.c utils
	$(CC) -O2 utils.o -o staccatod staccatod.c
install: staccato staccatod
	mkdir -p $(PREFIX)/bin
	cp -f staccato $(PREFIX)/bin
	cp -f staccatod $(PREFIX)/bin
uninstall:
	rm -f $(PREFIX)/bin/staccato
	rm -f $(PREFIX)/bin/staccatod
clean:
	rm -f utils.o staccato staccatod

.PHONY: clean uninstall
