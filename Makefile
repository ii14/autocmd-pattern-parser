CFLAGS = -Wall -Wextra

all: auparser

%.o: %.c auparser.h
	$(CC) $(CFLAGS) -c -o $@ $<

test.o: bdd-for-c.h

auparser: main.o auparser.o
	$(CC) $(CFLAGS) -o auparser main.o auparser.o $(LDFLAGS)

tests: test.o auparser.o
	$(CC) $(CFLAGS) -o tests test.o auparser.o $(LDFLAGS)

test: tests
	./tests

clean:
	rm -rvf auparser.o main.o test.o

.PHONY: all test clean
