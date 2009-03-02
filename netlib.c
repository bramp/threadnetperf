#include "netlib.h"

#ifndef WIN32
#include <sys/ioctl.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int set_opt(SOCKET s, int level, int optname, int one) {
	if ( s == INVALID_SOCKET )
		return SOCKET_ERROR;

	return setsockopt(s, level, optname, (char *)&one, sizeof(one));
}

int enable_nagle(SOCKET s) {
	return set_opt(s, IPPROTO_TCP, TCP_NODELAY, 1);
}

int disable_nagle(SOCKET s) {
	return set_opt(s, IPPROTO_TCP, TCP_NODELAY, 0);
}

#ifndef WIN32
int enable_maxseq(SOCKET s, int size) {
	return set_opt(s, IPPROTO_TCP, TCP_MAXSEG, size + 52);
}

int disable_maxseq(SOCKET s) {
	return set_opt(s, IPPROTO_TCP, TCP_MAXSEG, 0);
}

int enable_timestamp(SOCKET s) {
	return set_opt(s, SOL_SOCKET, SO_TIMESTAMPNS, 1);
}

int disable_timestamp(SOCKET s) {
	return set_opt(s, SOL_SOCKET, SO_TIMESTAMPNS, 0);
}

/* Returns the timestamp in nanoseconds 
	Can be used when SO_TIMESTAMPNS does not work
*/
unsigned long long get_packet_timestamp(SOCKET s) {
	struct timespec ts = {0,0};
	if ( ioctl(s, SIOCGSTAMPNS, &ts) )
		return 0;

	if ( ts.tv_sec < 0 ) {
		printf("%ld %ld\n", ts.tv_sec, ts.tv_nsec);
		return 0;
	}

	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}


#endif

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

	if ( s == INVALID_SOCKET )
		return SOCKET_ERROR;

    if (size > 0 && setsockopt(s, SOL_SOCKET, opt, (char *)&size, sizeof(size)) < 0)
      return SOCKET_ERROR;

	if (getsockopt(s, SOL_SOCKET, opt, (char *)&new_size, &new_size_len) < 0)
		return SOCKET_ERROR;

 	return new_size;
}

// Sets the socket's send, and returns the new size
int set_socket_send_buffer(SOCKET s, unsigned int socket_size) {
	return set_socket_buffer(s, SO_SNDBUF, socket_size);
}

int set_socket_recv_buffer(SOCKET s, unsigned int socket_size) {
	return set_socket_buffer(s, SO_RCVBUF, socket_size);
}

// Sets the socket's send/recv timeout
int set_socket_timeout(SOCKET s, unsigned int milliseconds) {
	struct timeval tv;

	if ( s == INVALID_SOCKET )
		return SOCKET_ERROR;

	tv.tv_sec = milliseconds / 1000;
	tv.tv_usec = milliseconds % 1000 * 1000;

    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)))
      return SOCKET_ERROR;

    if (setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)))
      return SOCKET_ERROR;

	return 0;
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


SOCKET highest_socket(SOCKET *s, size_t len) {

	const SOCKET *s_max = s + len;
	SOCKET max;

	assert ( s != NULL );

	if ( len == 0 )
		return SOCKET_ERROR;

	// Update the nfds
	max = *s++;

	// Loop all client sockets
	for ( ; s < s_max ; s++) {
		assert ( *s != INVALID_SOCKET );

		if ( *s > max )
			max = *s;
	}

	return max;
}

/**
	Turn a addr into an string representing its address
*/
char * addr_to_ipstr(const struct sockaddr *addr, socklen_t addlen, char *host, size_t maxhostlen) {

	char port [ NI_MAXSERV ];

	// Validate parameters
    assert (addr != NULL);
	assert (addlen > 0 );

	assert (host != NULL);
	assert (maxhostlen > 0);

	if ( getnameinfo (addr, addlen, host, maxhostlen, port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV ) ) {
		*host = '\0';
		return NULL;
	}

	// Add the port #
	strncat(host, ":", maxhostlen);
	strncat(host, port, maxhostlen);

	return host;
}

/**
	Turns a hostname into a address struct
**/
int str_to_addr(const char *host, struct sockaddr *addr, socklen_t *addlen) {
	struct addrinfo *aiList = NULL;

	assert ( host != NULL );
	assert ( addr != NULL );
	assert ( addlen != NULL );

	if ( getaddrinfo(host, NULL, NULL, &aiList) ) {
		return -1;
	}

	*addr = *(aiList->ai_addr);
	*addlen = (socklen_t) aiList->ai_addrlen;

	freeaddrinfo( aiList );

	return 0;
}

inline int connect_ign_signal(int sockfd, const struct sockaddr *serv_addr, socklen_t addrlen) {
	int len;
	while ( 1 ) {
		len = connect( sockfd, serv_addr, addrlen);
		if ( len == SOCKET_ERROR && ERRNO == EINTR )
			continue;
		return len;
	}
}

inline int accept_ign_signal(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	int len;
	while ( 1 ) {
		len = accept( sockfd, addr, addrlen);
		if ( len == SOCKET_ERROR && ERRNO == EINTR )
			continue;
		return len;
	}
}

inline ssize_t recv_ign_signal(int s, void *buf, size_t len, int flags) {
	int ret;
	while ( 1 ) {
		ret = recv( s, buf, len, flags);
		if ( ret == SOCKET_ERROR && ERRNO == EINTR )
			continue;
		return ret;
	}
}

inline ssize_t recvmsg_ign_signal(int s, struct msghdr *msg, int flags) {
	int len;
	while ( 1 ) {
		len = recvmsg(s, msg, flags);
		if ( len == SOCKET_ERROR && ERRNO == EINTR )
			continue;
		return len;
	}
}

inline ssize_t send_ign_signal(int s, const void *buf, size_t len, int flags) {
	int ret;
	while ( 1 ) {
		ret = send( s, buf, len, flags);
		if ( ret == SOCKET_ERROR && ERRNO == EINTR )
			continue;
		return ret;
	}
}

inline int select_ign_signal(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
	int num_fd;
	while ( 1 ) {
		num_fd = select(nfds, readfds, writefds, exceptfds, timeout);
		if ( num_fd == SOCKET_ERROR && ERRNO == EINTR )
			continue;
		return num_fd;
	}
}

inline int close_ign_signal(int fildes) {
	int err;
	while ( 1 ) {
		err = close(fildes);
		if ( err == SOCKET_ERROR && ERRNO == EINTR )
			continue;
		return err;
	}
}

#ifdef USE_EPOLL
inline int epoll_wait_ign_signal(int epfd, struct epoll_event * events, int maxevents, int timeout) {
	int num_fd;
	while ( 1 ) {
		num_fd = epoll_wait(epfd, events, maxevents, timeout);
		if ( num_fd == SOCKET_ERROR && ERRNO == EINTR )
			continue;
		return num_fd;
	}
}
#endif
