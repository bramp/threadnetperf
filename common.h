#ifndef _COMMON_H
#define _COMMON_H

#define _GNU_SOURCE

//Used to turn on the checking of the microseconds
//#define CHECK_TIMES 100000
#include "netlib.h"
#include <signal.h>   // for SIGRTMIN

// The number of cores this machine has
extern const unsigned int max_cores; // TODO get the real number!

struct stats {
	// The cores these stats were recorded from
	unsigned int cores;

	// The number of bytes received
	unsigned long long bytes_received;

	// The number of recv() handled
	unsigned long long pkts_received;

	// The duration packets were inside the network
	unsigned long long pkts_time;

	// Number of timestamps received
	unsigned long long timestamps;

	// The duration over which these stats were recorded
	unsigned long long duration;

#ifdef CHECK_TIMES
	//Temp buffer for the recv time values used to plot a histogram
	float processing_times[CHECK_TIMES];
	int processed_something;
#endif

};

struct test { 
	unsigned int clientcores; // The cores the client may run on
	unsigned int servercores; // The cores the servers may run on

	unsigned int connections; // The number of connections

	struct sockaddr_storage addr;
	socklen_t addr_len;
};

// Settings
struct settings {

	unsigned int duration;

	int type;
	int protocol;

	unsigned int daemon          :1;
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

	unsigned int rate; // Sends per second

	const char *server_host;
	unsigned short port;

	// An array of each test
	unsigned int tests;
	struct test *test;

	// Helper members, so we don't have to recalc all the time
	unsigned int clientcores;
	unsigned int servercores;

	unsigned int threaded_model;
};

// Works out how many cores the client will use
unsigned int count_client_cores( const struct test *test, const unsigned int tests );

// Works out how many cores the server will use
unsigned int count_server_cores( const struct test *test, const unsigned int tests );

void stats_add(struct stats *dest, const struct stats *src);

// Move all the elements after arr down one
void move_down ( SOCKET *arr, SOCKET *arr_end );

unsigned long long get_microseconds();

unsigned long long get_nanoseconds();

void get_timespec_now(struct timespec *ts);
void add_timespec(struct timespec *ts, time_t tv_sec, long tv_nsec);

void stop_all();

double calc_confidence(double confidence_lvl, double mean, double variance, unsigned int n, int verbose);


#ifdef WIN32
// Sleep for a number of microseconds
int usleep(unsigned int useconds);
#endif

// Returns the highest socket in the set
SOCKET highest_socket(SOCKET *s, size_t len);

// Malloc a 2D array
void **malloc_2D(size_t element_size, size_t x, size_t y);
void free_2D(void **data, size_t x);

#define BUFFER_FILL 0x41

// How long do control sockets wait for connections (in milliseconds)
#define CONTROL_TIMEOUT 30000

// How long to wait for IPC socket calls
#define IPC_TIMEOUT 5000

// How long the server waits for a connection (in milliseconds)
#define TRANSFER_TIMEOUT 1000

#define SIGNUM ( SIGRTMIN + 5 )

#define SIGNAL_READY_TO_ACCEPT		1
#define SIGNAL_READY_TO_GO			2
#define SIGNAL_GO					3
#define SIGNAL_STOP					4

#define MODEL_THREADED 	0
#define MODEL_PROCESS 	1

#define IPC_SOCK_NAME "resultspipe"

#endif
