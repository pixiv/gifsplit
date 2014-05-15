CFLAGS ?= -O2 -pipe -march=native


all: gifsplit

%.o: %.c
	$(CC) -Wall -std=c99 $(CFLAGS) -c -o $@ $<

gifsplit: gifsplit.o libgifsplit.o
	$(CC) -Wall -std=c99 $(CFLAGS) -o $@ gifsplit.o libgifsplit.o -lgif -lpng

clean:
	-rm -f gifsplit *.o
