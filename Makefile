CFLAGS = -Wall -Wextra -g

all: auparser

%.o: %.c auparser.h
	$(CC) $(CFLAGS) -c -o $@ $<

auparser.o: auparser.h
main.o: auparser.h
test.o: auparser.h

auparser: main.o auparser.o
	$(CC) $(CFLAGS) -o auparser main.o auparser.o $(LFLAGS)
test: test.o auparser.o
	$(CC) $(CFLAGS) -o test test.o auparser.o $(LFLAGS)

.PHONY: all
