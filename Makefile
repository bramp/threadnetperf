
#INCLUDES = -I
CC = gcc
CFLAGS = -g -O0 -c -Wall $(INCLUDES) -DTHREAD_SAFE -D_REENTRANT
LDFLAGS = -pthread

SOURCES =	threadnetperf.c

OBJECTS=$(SOURCES:.c=.o)

EXECUTABLE=threadnetperf

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CPP) $(CFLAGS) $< -o $@
	
clean:
	rm -f ${OBJECTS} $(EXECUTABLE)

