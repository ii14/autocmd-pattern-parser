CFLAGS = -Wall -Wextra -g

all: auparser

%.o: %.c auparser.h
	$(CC) $(CFLAGS) -c -o $@ $<

auparser: main.o auparser.o
	$(CC) $(CFLAGS) -o auparser main.o auparser.o $(LFLAGS)

test: test.o auparser.o
	$(CC) $(CFLAGS) -o test test.o auparser.o $(LFLAGS)

clean:
	rm -rvf auparser.o main.o test.o

.PHONY: all clean
