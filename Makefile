
#INCLUDES = -I
CC = gcc
#CFLAGS = -g -O0 -c -W -Wall -Wconversion -Wshadow -Wcast-qual -Wwrite-strings  $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT -D_DEBUG
#CFLAGS = -g -O0 -c -Wall  $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT -D_DEBUG
CFLAGS = -O3 -march=native -c -Wall $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT
LDFLAGS = -pthread -lm -lrt

SOURCES =	threadnetperf.c common.c server_thread.c client_thread.c server.c client.c print.c remote.c serialise.c threads.c parse.c

OBJECTS=$(SOURCES:.c=.o)

EXECUTABLE=threadnetperf

all: version.h $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@
	
clean:
	rm -f version.h ${OBJECTS} $(EXECUTABLE)

version.h:
	/bin/sh ./version.sh

