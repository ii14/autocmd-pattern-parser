CFLAGS = -Wall -Wextra -g

all: auparser

%.o: %.c auparser.h
	$(CC) $(CFLAGS) -c -o $@ $<

auparser: main.o auparser.o
	$(CC) $(CFLAGS) -o auparser main.o auparser.o $(LFLAGS)

tests: test.o auparser.o
	$(CC) $(CFLAGS) -o tests test.o auparser.o $(LFLAGS)

test: tests
	./tests

clean:
	rm -rvf auparser.o main.o test.o

.PHONY: all test clean
