CC = gcc
CFLAGS = -Og
LDLIBS = -lpthread

PROGS = myShell

all: $(PROGS)

shellex: myShell.c csapp.c

clean:
	rm -rf *~ $(PROGS)

