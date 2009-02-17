
#INCLUDES = -I
CC = gcc
#CFLAGS = -g -O0 -c -W -Wall -Wconversion -Wshadow -Wcast-qual -Wwrite-strings  $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT -D_DEBUG
CFLAGS = -g -O0 -c -Wall  $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT -D_DEBUG -DUSE_EPOLL 
#-DMF_FLIPPAGE
#CFLAGS = -O3 -c -Wall $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT -DUSE_EPOLL -DMF_FLIPPAGE 
#CFLAGS = -O3 -c -Wall $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT -DUSE_EPOLL -DMF_NOCOPY
#CFLAGS = -O3 -c -Wall $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT -DUSE_EPOLL
LDFLAGS = -pthread -lm -lrt

SOURCES =	threadnetperf.c common.c server_thread.c client_thread.c server.c client.c print.c remote.c serialise.c threads.c parse.c netlib.c

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

