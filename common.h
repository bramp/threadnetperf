#ifndef _COMMON_H
#define _COMMON_H

#define _GNU_SOURCE 

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

struct server_request {
	unsigned short port;

	unsigned int n; // The number of connections to accept

	// Stats
	unsigned long long bytes_received;
	unsigned long long pkts_received;
	unsigned long long duration;

	unsigned int core; // Which core this server is running on
};

// Struct to pass to a client thread
struct client_request {

	// The address to connect to
	struct sockaddr *addr;
	int addr_len;

	unsigned int n; // The number of connection to create

	struct client_request *next;
};

void *server_thread(void *data);
void *client_thread(void *data);

int enable_nagle(SOCKET s);
int disable_nagle(SOCKET s);

// Move all the elements after arr down one
void move_down ( SOCKET *arr, SOCKET *arr_end );

unsigned long long get_microseconds();

size_t addr_to_ipstr(const struct sockaddr *addr, socklen_t addlen, char *host, size_t maxhostlen);

#endif
