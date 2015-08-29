CC=gcc
CFLAGS=-std=c99 -O2 -g -pipe -Wall -Wextra
LDFLAGS=-Wl,-O1

SRC=main.c fftw.c
HEAD=jack2libstlseries.h
OBJ=$(SRC:.c=.o)

.PHONY: all

all: jack2libstlseries

jack2libstlseries: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS) $(shell pkg-config jack --libs) $(shell pkg-config fftw3 --libs) -lm -lstlseries

%.o: %.c $(HEAD)
	$(CC) -c $< $(CFLAGS) $(shell pkg-config jack --cflags) $(shell pkg-config fftw3 --libs)

clean:
	rm -f $(OBJ) jack2libstlseries
