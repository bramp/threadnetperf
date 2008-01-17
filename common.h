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
#endif


struct stats {
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
	unsigned int duration;
	
	int type;
	int protocol;
	
	int deamon;
	int verbose;
	int dirty;
	int timestamp;
	int disable_nagles;

	unsigned int confidence_lvl;
	unsigned int min_iterations;
	unsigned int max_iterations;
	unsigned int message_size;	
	unsigned int socket_size;

	const char *server_host;
	unsigned short port;
};

struct server_request {
 
	volatile int bRunning; // Flag to indicate if the server should be running

	unsigned short port; // The port the server is listening on
	unsigned int n; // The number of connections to accept

	unsigned int core; // Which core this server is running on

	const struct settings *settings;

	// Stats
	struct stats stats;
};

// Struct to pass to a client thread
struct client_request {

	volatile int bRunning; // Flag to indicate if the client should be running

	unsigned int core; // Which core this client is running on

	const struct settings *settings;
	struct client_request_details *details;
};

// One of these for each destination this client thread connects to
struct client_request_details {

	// The address to connect to
	struct sockaddr *addr;
	int addr_len;

	unsigned int n; // The number of connection to create

	struct client_request_details *next;
};

void *client_thread(void *data);

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
