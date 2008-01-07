
#INCLUDES = -I
CC = gcc
CFLAGS = -g -O0 -c -Wall $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT -D_DEBUG
#CFLAGS = -O2 -c -Wall $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT
LDFLAGS = -pthread

SOURCES =	threadnetperf.c common.c server.c client.c

OBJECTS=$(SOURCES:.c=.o)

EXECUTABLE=threadnetperf

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@
	
clean:
	rm -f ${OBJECTS} $(EXECUTABLE)

