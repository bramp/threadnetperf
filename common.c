#include "common.h"

int enable_nagle(SOCKET s) {
	int one = 0;
	return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));
}

int disable_nagle(SOCKET s) {
	int one = 1;
	return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));
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

/**
	Turn a addr into an string representing its address
*/
size_t addr_to_ipstr(const struct sockaddr *addr, socklen_t addlen, char *host, size_t maxhostlen) { 
    // Validate parameters
    assert (host != NULL);
	assert (maxhostlen != 0);
	assert (addr != 0);

	// Error
	if ( getnameinfo (addr, addlen, host, maxhostlen, NULL, 0, NI_NUMERICHOST) ) {
		*host = '\0';
		return 0;
	}

	return strlen(host);
}

