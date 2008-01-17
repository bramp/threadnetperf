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
