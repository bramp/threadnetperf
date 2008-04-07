#ifndef _COMMON_H
#define _COMMON_H

#define _GNU_SOURCE

//Used to turn on the checking of the microseconds
//#define CHECK_TIMES 100000
#include "netlib.h"

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

	// Number of errors when using timestampe
	unsigned int time_err;

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

	unsigned int deamon          :1;
	unsigned int verbose         :1;
	unsigned int dirty           :1;
	unsigned int timestamp       :1;
	unsigned int disable_nagles  :1;

	double confidence_lvl;
	double confidence_int;
	unsigned int min_iterations;
	unsigned int max_iterations;
	unsigned int message_size;
	unsigned int socket_size;

	const char *server_host;
	unsigned short port;

	// A 2D array for each possible to and from core (with number of connections)
	unsigned int cores;
	unsigned int **clientserver;
};

// Works out how many cores the client will use
unsigned int count_client_cores( unsigned int **clientserver, unsigned int cores );

// Works out how many cores the server will use
unsigned int count_server_cores( unsigned int **clientserver, unsigned int cores );

void stats_add(struct stats *dest, const struct stats *src);

int enable_timestamp(SOCKET s);
int disable_timestamp(SOCKET s);

int enable_nagle(SOCKET s);
int disable_nagle(SOCKET s);

int enable_blocking(SOCKET s);
int disable_blocking(SOCKET s);

int set_socket_send_buffer(SOCKET s, unsigned int socket_size);
int set_socket_recv_buffer(SOCKET s, unsigned int socket_size);

int set_socket_timeout(SOCKET s, unsigned int milliseconds);

unsigned long long get_packet_timestamp(SOCKET s);

// Move all the elements after arr down one
void move_down ( SOCKET *arr, SOCKET *arr_end );

unsigned long long get_microseconds();

unsigned long long get_nanoseconds();

char * addr_to_ipstr(const struct sockaddr *addr, socklen_t addlen, char *host, size_t maxhostlen);

void stop_all();

double calc_confidence(double confidence_lvl, double mean, double variance, unsigned int n, int verbose);

#ifdef WIN32
void cleanup_winsock();
void setup_winsock();

// Sleep for a number of microseconds
int usleep(unsigned int useconds);
#endif

// Returns the highest socket in the set
SOCKET highest_socket(SOCKET *s, size_t len);

// Malloc a 2D array
void **malloc_2D(size_t element_size, size_t x, size_t y);
void free_2D(void **data, size_t x);

#define BUFFER_FILL 0x4141414141414141

// How long do control sockets wait for connections
#define CONTROL_TIMEOUT 30000

#endif
