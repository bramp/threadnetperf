#ifndef _COMMON_H
#define _COMMON_H

#define _GNU_SOURCE

//Used to turn on the checking of the microseconds 
//#define CHECK_TIMES 100000
#include "netlib.h"

#include <pthread.h> // We assume we have a pthread library (even on windows)

#ifdef WIN32
	// Define some dummy structs, currently they do nothing
	typedef struct {
		unsigned long int __cpu_mask;
	} cpu_set_t;

	/* Access functions for CPU masks.  */
	#define CPU_ZERO(cpusetp)
	#define CPU_SET(cpu, cpusetp)
	#define CPU_CLR(cpu, cpusetp)
	#define CPU_ISSET(cpu, cpusetp)
#endif


struct stats {
	// The core these stats were recorded from
	unsigned int core;

	// The number of bytes received
	unsigned long long bytes_received;
	
	// The number of recv() handled
	unsigned long long pkts_received;

	// The duration packets were inside the network
	unsigned long long pkts_time;	
	
	// The duration over which these stats were recorded
	unsigned long long duration;
	
#ifdef CHECK_TIMES
	//Temp buffer for the recv time values used to plot a histogram
	float processing_times[CHECK_TIMES];
	int processed_something;
#endif

};

// Settings
struct settings {

	// Version of the setting struct, this must be the first element
	unsigned int version;
	#define SETTINGS_VERSION 2 // Increment this each time the setting struct changes

	unsigned int duration;
	
	int type;
	int protocol;
	
	int deamon;
	int verbose;
	int dirty;
	int timestamp;
	int disable_nagles;

	//MF: Changed from unsigned in to a float, we can now handle
	// 99.95 etc -Is this int he right place now? It's not alligned?
	float confidence_lvl;
	unsigned int min_iterations;
	unsigned int max_iterations;
	unsigned int message_size;	
	unsigned int socket_size;

	const char *server_host;
	unsigned short port;

	// A 2D array for each possible to and from core (with number of connections)
	int cores;
	int **clientserver;
};

int enable_nagle(SOCKET s);
int disable_nagle(SOCKET s);

int enable_blocking(SOCKET s);
int disable_blocking(SOCKET s);

int set_socket_send_buffer(SOCKET s, unsigned int socket_size);
int set_socket_recv_buffer(SOCKET s, unsigned int socket_size);

// Move all the elements after arr down one
void move_down ( SOCKET *arr, SOCKET *arr_end );

unsigned long long get_microseconds();

char * addr_to_ipstr(const struct sockaddr *addr, socklen_t addlen, char *host, size_t maxhostlen);

void stop_all();

int pthread_create_on( pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset);

float calc_confidence(unsigned int confidence_lvl, float mean, float variance, unsigned int num_samples, int verbose);

#ifdef WIN32
void cleanup_winsock();
void setup_winsock();

// Sleep for a number of microseconds
int usleep(unsigned int useconds);
#endif

// Returns the highest socket in the set
SOCKET highest_socket(SOCKET *s, size_t len);

#define BUFFER_FILL 0x4141414141414141

#endif
