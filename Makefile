.POSIX:

PREFIX = /usr/local
CC = gcc

dwmbricks: dwmbricks.o
	$(CC) dwmbricks.o -lX11 -o dwmbricks
dwmbricks.o: dwmbricks.c config.h
	$(CC) -c dwmbricks.c
clean:
	rm -f *.o *.gch dwmbricks
install: dwmbricks
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f dwmbricks $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/dwmbricks
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dwmbricks

.PHONY: clean install uninstall
