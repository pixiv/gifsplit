CFLAGS ?= -O2 -pipe -march=native
PREFIX ?= /usr/local
PACKAGE = gifsplit
VERSION = 0.2

all: gifsplit

%.o: %.c
	$(CC) -DVERSION=\"$(VERSION)\" -Wall -std=c99 $(CFLAGS) -c -o $@ $<

gifsplit: gifsplit.o libgifsplit.o
	$(CC) -Wall -std=c99 $(CFLAGS) -o $@ gifsplit.o libgifsplit.o -lgif -lpng -ljpeg

clean:
	-rm -f gifsplit *.o

install: all
	install -D gifsplit $(PREFIX)/bin/gifsplit

dist:
	git archive --format=tar.gz --prefix=$(PACKAGE)-$(VERSION)/ HEAD > $(PACKAGE)-$(VERSION).tar.gz

