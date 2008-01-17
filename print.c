// Code that prints out to the user
#include "print.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <assert.h>

#ifdef WIN32
	#define snprintf _snprintf
#endif

// Printf Mutex, to stop printing ontop of each other
pthread_mutex_t printf_mutex = PTHREAD_MUTEX_INITIALIZER;

void print_headers(const struct settings* settings) {

	assert ( settings != NULL );

	pthread_mutex_lock( &printf_mutex );

	printf("Core\tsend\treceived\tnum\ttime\tgoodput%s\n",
		settings->timestamp ? "\tpacket" : "");

	printf("\tmsg\tbytes\t\trecv()s\t\t(MB/s)\t%s\n",
		settings->timestamp ? "\tlatency" : "");

	printf("\tsize\t\t\t\t\t\t\n");

	pthread_mutex_unlock( &printf_mutex );
}

void print_results( const struct settings *settings, struct stats *stats ) {
	float thruput = stats->bytes_received > 0 ? (float)stats->bytes_received / (float)stats->duration : 0;
	float duration = (float)stats->duration / (float)1000000;
//	float pkt_latency = (float)stats->pkts_time /  (float)stats->pkts_received;

	assert ( settings != NULL );
	assert ( stats != NULL );

	pthread_mutex_lock( &printf_mutex );

#ifdef WIN32 // Work around a silly windows bug in handling %llu
	printf( "%i\t%u\t%I64u\t%I64u\t%.2fs\t%.2f", 
#else
	printf( "%i\t%u\t%llu\t%llu\t%.2fs\t%.2f",
#endif
		stats->core, settings->message_size, stats->bytes_received, stats->pkts_received, duration, thruput );

	if ( settings->timestamp )
		printf( "\t%lluus",stats->pkts_time );

	printf("\n");
	
#ifdef CHECK_TIMES 
{ 	
	int i;
	if(stats->processed_something) {
		for(i=0; i<CHECK_TIMES && i < stats-> pkts_received; i++ ) {
	
			printf("%f\n", stats->processing_times[i]);
		} 
	}
}
	printf("\n");
#endif

	pthread_mutex_unlock( &printf_mutex );
}

void print_stats(unsigned long long sum, unsigned long long sumsquare, unsigned long long mean, double variance) {
	printf("sum %llu sumsquare %llu mean %llu variance %f\n",sum, sumsquare, mean, variance);
}

/*
 * Prints the memory pointed to by @data in hex format for @size bytes
 */
void print_hex(void *data, int size){
    unsigned char *p = data;
    unsigned char c;
    int n;
    char bytestr[4] = {0};
    char addrstr[10] = {0};
    char hexstr[ 16*3 + 5] = {0};
    char charstr[16*1 + 5] = {0};

	assert ( data != NULL );

    for(n=1;n<=size;n++) {
        if (n%16 == 1) {
            /* store address for this line */
            snprintf(addrstr, sizeof(addrstr), "%.4x",
               (unsigned int)(p- (unsigned char*) data) );
        }
            
        c = *p;
        if (isalnum(c) == 0) {
            c = '.';
        }

        /* store hex str (for left side) */
        snprintf(bytestr, sizeof(bytestr), "%02X ", *p);
        strncat(hexstr, bytestr, sizeof(hexstr)-strlen(hexstr)-1);

        /* store char str (for right side) */
        snprintf(bytestr, sizeof(bytestr), "%c", c);
        strncat(charstr, bytestr, sizeof(charstr)-strlen(charstr)-1);

        if(n%16 == 0) { 
            /* line completed */
            printf("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
            hexstr[0] = 0;
            charstr[0] = 0;
        } else if(n%8 == 0) {
            /* half line: add whitespaces */
            strncat(hexstr, "  ", sizeof(hexstr)-strlen(hexstr)-1);
            strncat(charstr, " ", sizeof(charstr)-strlen(charstr)-1);
        }
        p++; /* next byte */
    }

    if (strlen(hexstr) > 0) {
        /* print rest of buffer if not empty */
        printf("[%4.4s]   %-50.50s  %s\n", addrstr, hexstr, charstr);
    }
}

