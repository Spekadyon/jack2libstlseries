CC=gcc
CFLAGS=-std=c99 -O2 -g -pipe -Wall -Wextra
LDFLAGS=-Wl,-O1


.PHONY: all

all: jack2libstlseries

jack2libstlseries: jack2libstlseries.o
	$(CC) -o $@ $< $(LDFLAGS) $(shell pkg-config jack --libs) $(shell pkg-config fftw3 --libs) -lm -lstlseries

jack2libstlseries.o: jack2libstlseries.c
	$(CC) -c $< $(CFLAGS) $(shell pkg-config jack --cflags) $(shell pkg-config fftw3 --libs)

clean:
	rm -f jack2libstlseries.o jack2libstlseries
