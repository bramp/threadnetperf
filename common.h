#ifndef _COMMON_H
#define _COMMON_H

#define _GNU_SOURCE

//Used to turn on the checking of the microseconds 
//#define CHECK_TIMES 100000

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <malloc.h>

#include <time.h>

#ifdef WIN32
	#define WIN32_LEAN_AND_MEAN

	#include "winsock2.h"
	#include "Ws2tcpip.h"

	#include "getopt.h"

	#define ERRNO (WSAGetLastError())
	#define ECONNRESET WSAECONNRESET
	#define EWOULDBLOCK WSAEWOULDBLOCK

	#define SHUT_RDWR SD_BOTH

	// Define some dummy structs, currently they do nothing
	typedef struct {
		unsigned long int __cpu_mask;
	} cpu_set_t;

	/* Access functions for CPU masks.  */
	#define CPU_ZERO(cpusetp)
	#define CPU_SET(cpu, cpusetp)
	#define CPU_CLR(cpu, cpusetp)
	#define CPU_ISSET(cpu, cpusetp)

	#define snprintf _snprintf

#else

	#include <errno.h>
	#include <sys/time.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h> // TCP_NODELAY, TCP_MAXSEG
	#include <unistd.h> // for getopt
	#include <arpa/inet.h> // For inet_addr
	#include <netdb.h> // For NI_NUMERICHOST
	#include <fcntl.h>

	#define ERRNO errno
	#define closesocket(s) close(s)

	#ifndef SOCKET
		#define SOCKET int
		#define INVALID_SOCKET (-1)
		#define SOCKET_ERROR (-1)
	#endif
#endif

#include <pthread.h> // We assume we have a pthread library (even on windows)
#include <semaphore.h>
#include <sched.h>

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

	unsigned int message_size;	
	unsigned int socket_size;

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

	// The address to connect to
	struct sockaddr *addr;
	int addr_len;

	const struct settings *settings;

	unsigned int n; // The number of connection to create
	unsigned int core; // Which core this server is running on

	struct client_request *next;
};

void *server_thread(void *data);
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

size_t addr_to_ipstr(const struct sockaddr *addr, socklen_t addlen, char *host, size_t maxhostlen);

void print_results( const struct settings * settings, int core, struct stats *stats );

void print_headers( const struct settings * settings );

void print_hex(void *data, int size);

void stop_all();

int pthread_create_on( pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset);

#ifdef WIN32
void cleanup_winsock();
void setup_winsock();

// Sleep for a number of microseconds
int usleep(unsigned int useconds);

#endif
	
#define BUFFER_FILL 0x4141414141414141

#endif
