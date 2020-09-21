.POSIX:

PREFIX = /usr/local
CC = gcc

dwmbricks: dwmbricks.o
	$(CC) dwmbricks.o -lX11 -o dwmbricks
dwmbricks.o:
	$(CC) -g -Og -c dwmbricks.c
clean:
	rm -f *.o *.gch dwmbricks
install: dwmbricks
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f dwmbricks $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/dwmbricks
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dwmbricks

.PHONY: dwmbricks.o clean install uninstall
