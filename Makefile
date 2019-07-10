ifeq ($(OS),Windows_NT)
    OBJEXT = .obj
    BINEXT = .exe
    CC = bcc32
    CFLAGS = 
else
    OBJEXT = .o
    BINEXT = 
    CC = gcc
    CFLAGS = -g -Wall
endif

LIBS=					
				
cpm: cpm.o
	$(CC) $(LIBS) cpm$(OBJEXT) -o cpm$(BINEXT)
					
cpm.o: cpm.c
	$(CC) -c $(CFLAGS) cpm.c

clean:
	$(RM) cpm$(OBJEXT)
					
