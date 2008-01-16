#include "common.h"

#include <stdio.h>
#include <ctype.h>
#include <assert.h>

/* Access functions for CPU masks.  */
#define CPU_ZERO(cpusetp)
#define CPU_SET(cpu, cpusetp)
#define CPU_CLR(cpu, cpusetp)
#define CPU_ISSET(cpu, cpusetp)

int enable_nagle(SOCKET s) {
	int zero = 0;
	return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&zero, sizeof(zero));
}

int disable_nagle(SOCKET s) {
	int one = 1;
	return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));
}

int enable_blocking(SOCKET s) {
#ifdef WIN32
	unsigned long flags = 0;
	return ioctlsocket(s, FIONBIO, &flags);
#else
	return fcntl(s, F_SETFL, O_NONBLOCK);
#endif
}

int disable_blocking(SOCKET s) {
#ifdef WIN32
	unsigned long flags = 1;
	return ioctlsocket(s, FIONBIO, &flags);
#else
	return fcntl(s, F_SETFL, O_NONBLOCK);
#endif
}

int set_socket_buffer( SOCKET s, int opt, int size ) {

    int new_size;
    socklen_t new_size_len = sizeof(new_size);
 
    if (size > 0 && setsockopt(s, SOL_SOCKET, opt, (char *)&size, sizeof(size)) < 0) {
      return -1;
    }

	if (getsockopt(s, SOL_SOCKET, opt, (char *)&new_size, &new_size_len) < 0) {
		return -1;
 	}

 	return new_size;
}

// Sets the socket's send, and returns the new size
int set_socket_send_buffer(SOCKET s, unsigned int socket_size) {
	return set_socket_buffer(s, SO_SNDBUF, socket_size);
}

int set_socket_recv_buffer(SOCKET s, unsigned int socket_size) {
	return set_socket_buffer(s, SO_RCVBUF, socket_size);
}

// Move all the elements after arr down one
void move_down ( SOCKET *arr, SOCKET *arr_end ) {

	// Check this socket isn't already invalid
	assert ( *arr != INVALID_SOCKET );

	*arr = INVALID_SOCKET;

	// Move any other clients down
	while ( (arr + 1) < arr_end ) {
		*arr = *(arr + 1);
		arr++;

		// Check we didn't just copy a INVALID_SOCKET
		assert ( *arr != INVALID_SOCKET );
	}
}

// Returns the number of microseconds since a epoch
unsigned long long get_microseconds() {
	unsigned long long microseconds = 0;

#ifdef WIN32
	FILETIME ft;

	GetSystemTimeAsFileTime(&ft);

	microseconds |= ft.dwHighDateTime;
	microseconds <<= 32;
	microseconds |= ft.dwLowDateTime;

	microseconds /= 10;	//convert into microseconds
#else
	struct timeval tv;

	gettimeofday(&tv, NULL); 
	microseconds = tv.tv_sec * 1000000 + tv.tv_usec;

#endif

	return microseconds;
}

#ifdef WIN32
	int pthread_attr_setaffinity_np ( pthread_attr_t *attr, size_t cpusetsize, const cpu_set_t *cpuset) {
		// TODO Make this set affidenitys on windows
		return 0;
	}
#endif

/**
	Create a thread on a specific core(s)
*/
int pthread_create_on( pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void*), void *arg, size_t cpusetsize, const cpu_set_t *cpuset) {

	pthread_attr_t thread_attr;
	int ret;

	if (attr == NULL) {
		pthread_attr_init ( &thread_attr );
		attr = &thread_attr;
	}

	// Set the CPU
	ret = pthread_attr_setaffinity_np( attr, cpusetsize, cpuset );
	if (ret)
		goto cleanup;

	// Make sure the thread is joinable
	ret = pthread_attr_setdetachstate( attr, PTHREAD_CREATE_JOINABLE);
	if (ret)
		goto cleanup;

	// Now create the thread
	ret = pthread_create(thread, attr, start_routine, arg);

cleanup:
	if ( attr == &thread_attr )
		pthread_attr_destroy ( &thread_attr );

	return ret;
}

#ifdef WIN32
/**
	Function to setup the winsock libs
*/
void setup_winsock() {
	WSADATA wsaData;

	if ( WSAStartup(MAKEWORD(2,2), &wsaData) ) {
		fprintf(stderr, "%s:%d WSAStartup() error\n", __FILE__, __LINE__ );
		return;
	}
}

void cleanup_winsock() {
	WSACleanup();
}
#endif

#ifdef WIN32
// Sleep for a number of microseconds
int usleep(unsigned int useconds) {
	struct timespec waittime;

	if ( useconds > 1000000 )
		return EINVAL;

	waittime.tv_sec = 0;
	waittime.tv_nsec = useconds * 1000; 

	pthread_delay_np ( &waittime );
	return 0;
}
#endif


/**
	Turn a addr into an string representing its address
*/
char * addr_to_ipstr(const struct sockaddr *addr, socklen_t addlen, char *host, size_t maxhostlen) { 
    
	char port [ NI_MAXSERV ];
	
	// Validate parameters
    assert (host != NULL);
	assert (maxhostlen != 0);
	assert (addr != 0);

	if ( getnameinfo (addr, addlen, host, maxhostlen, port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV ) ) {
		*host = '\0';
		return NULL;
	}

	// Add the port #
	strncat(host, ":", maxhostlen);
	strncat(host, port, maxhostlen);

	return host;
}

SOCKET highest_socket(SOCKET *s, size_t len) {

	const SOCKET *s_max = s + len;
	SOCKET max;

	if ( len == 0 )
		return SOCKET_ERROR;

	// Update the nfds
	max = *s;

	// Loop all client sockets
	for ( ; s < s_max ; s++) {
		assert ( *s != INVALID_SOCKET );

		if ( *s > max )
			max = *s;
	}

	return max;
}
