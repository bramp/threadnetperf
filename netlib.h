// Wraps all the network headers into one file for different OSes

#ifdef WIN32
	#define WIN32_LEAN_AND_MEAN

	#include "winsock2.h"
	#include "Ws2tcpip.h"

	#include "getopt.h"

	#define ERRNO (WSAGetLastError())
	#define ECONNRESET WSAECONNRESET
	#define EWOULDBLOCK WSAEWOULDBLOCK

	#define SHUT_RDWR SD_BOTH

#else
	#include <sys/time.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h> // TCP_NODELAY, TCP_MAXSEG
	#include <arpa/inet.h> // For inet_addr
	#include <netdb.h> // For NI_NUMERICHOST
	#include <fcntl.h>
	#include <errno.h> // For errno

	#define ERRNO errno
	#define closesocket(s) close(s)

	#define SOCKET int
	#define INVALID_SOCKET (-1)
	#define SOCKET_ERROR (-1)
#endif

int enable_timestamp(SOCKET s);
int disable_timestamp(SOCKET s);

int enable_nagle(SOCKET s);
int disable_nagle(SOCKET s);

int enable_blocking(SOCKET s);
int disable_blocking(SOCKET s);

int enable_maxseq(SOCKET s, int size);
int disable_maxseq(SOCKET s);

int set_socket_send_buffer(SOCKET s, unsigned int socket_size);
int set_socket_recv_buffer(SOCKET s, unsigned int socket_size);

int set_socket_timeout(SOCKET s, unsigned int milliseconds);

unsigned long long get_packet_timestamp(SOCKET s);

#ifdef WIN32
void cleanup_winsock();
void setup_winsock();
#endif

char * addr_to_ipstr(const struct sockaddr *addr, socklen_t addlen, char *host, size_t maxhostlen);
int str_to_addr(const char *host, struct sockaddr *addr, socklen_t *addlen);

