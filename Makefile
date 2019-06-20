CC=gcc
CFLAGS=-g -Wall
LIBS=					
				
cpm: cpm.o
	$(CC) $(LIBS) cpm.o -o cpm
					
cpm.o: cpm.c
	$(CC) -c $(CFLAGS) cpm.c

clean:
	rm cpm.o cpm.obj
					
