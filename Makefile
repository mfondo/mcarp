CC=gcc
CFLAGS=-c -Wall

mcarp: mcarp.o
	$(CC) mcarp.o -o mcarp

mcarp.o: mcarp.c
	$(CC) $(CFLAGS) -c mcarp.c

clean:
	rm -f *.o; rm -f mcarp
