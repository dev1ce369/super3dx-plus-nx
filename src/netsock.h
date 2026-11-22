/* UDP sockets the same on Winsock and POSIX (net.c, vs_host.c, nat.c): the types and the few calls that differ. */
#ifndef NETSOCK_H
#define NETSOCK_H

#ifdef __SWITCH__
#include <switch/result.h>
#include <switch/services/nifm.h>
#include <switch/services/ssl.h>
#include <switch/runtime/devices/socket.h>
#endif

/* One process-wide socket-stack init, in net.c (or net_stub.c). */
int net_sock_init(void);

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET net_fd;
#define NET_NOFD INVALID_SOCKET
#define net_fd_ok(fd) ((fd) != INVALID_SOCKET)
static inline void net_fd_close(net_fd fd) { closesocket(fd); }
static inline void net_fd_nonblock(net_fd fd) {
  u_long on = 1;
  ioctlsocket(fd, FIONBIO, &on);
}
/* a UDP port taken by no other socket (Winsock's SO_REUSEADDR would let a second one share it, and the packets go to either) */
static inline void net_fd_exclusive(net_fd fd) {
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&one, sizeof one);
}
typedef int net_len;
typedef int net_ssize;
#define NET_SOCKOPT(p) ((const char *)(p))
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int net_fd;
#define NET_NOFD (-1)
#define net_fd_ok(fd) ((fd) >= 0)
static inline void net_fd_close(net_fd fd) { close(fd); }
static inline void net_fd_nonblock(net_fd fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
static inline void net_fd_exclusive(net_fd fd) { (void)fd; } /* (a bound UDP port is refused to a second bind without SO_REUSEADDR) */
typedef socklen_t net_len;
typedef ssize_t net_ssize;
#define NET_SOCKOPT(p) (p)
#endif

#endif
