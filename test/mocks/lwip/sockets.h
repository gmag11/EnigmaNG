#pragma once
// lwip/sockets.h stub for native unit test builds.
// Provides minimal POSIX-like socket API used by DnsProxy.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

// On native Linux, include real socket headers to get sockaddr, fd_set, etc.
// This avoids redefinition conflicts.
#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>
#endif

// Address families and socket types
#ifndef AF_INET
#define AF_INET  2
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif
#ifndef INADDR_ANY
#define INADDR_ANY ((uint32_t)0u)
#endif
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif

#ifndef __linux__
// sockaddr types (only define on non-Linux where system headers not included)
struct in_addr {
    uint32_t s_addr;
};

struct sockaddr_in {
    uint16_t  sin_family;
    uint16_t  sin_port;
    in_addr   sin_addr;
    char      sin_zero[8];
};

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

typedef uint32_t socklen_t;

// fd_set / timeval / select macros
#ifndef FD_SETSIZE
#define FD_SETSIZE 64
#endif

typedef struct {
    uint64_t bits[FD_SETSIZE / 64];
} fd_set;

struct timeval {
    long tv_sec;
    long tv_usec;
};

#define FD_ZERO(s)    memset((s), 0, sizeof(*(s)))
#define FD_SET(fd,s)  ((s)->bits[(fd)/64] |= (1ULL << ((fd)%64)))
#define FD_ISSET(fd,s) (!!((s)->bits[(fd)/64] & (1ULL << ((fd)%64))))
#define FD_CLR(fd,s)  ((s)->bits[(fd)/64] &= ~(1ULL << ((fd)%64)))
#endif // !__linux__

// Byte-order helpers (host == little-endian assumed for native tests)
inline uint16_t lwip_htons(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
inline uint16_t lwip_ntohs(uint16_t x) { return lwip_htons(x); }
inline uint32_t lwip_htonl(uint32_t x) {
    return ((x & 0xFF) << 24) | (((x >> 8) & 0xFF) << 16) |
           (((x >> 16) & 0xFF) << 8) | ((x >> 24) & 0xFF);
}
inline uint32_t lwip_ntohl(uint32_t x) { return lwip_htonl(x); }

#ifndef htons
#define htons lwip_htons
#endif
#ifndef ntohs
#define ntohs lwip_ntohs
#endif
#ifndef htonl
#define htonl lwip_htonl
#endif
#ifndef ntohl
#define ntohl lwip_ntohl
#endif

// inet_addr helper
inline uint32_t lwip_inet_addr(const char* cp) {
    unsigned int a, b, c, d;
    if (sscanf(cp, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0xFFFFFFFFu;
    return lwip_htonl((a << 24) | (b << 16) | (c << 8) | d);
}
#ifndef inet_addr
#define inet_addr lwip_inet_addr
#endif

// ----- Stub socket API -----
// All stubs return failure so that native tests can compile and link.
// Tests that need real socket behaviour should use OS-native sockets directly.

// Track the last lwip_socket / lwip_bind / lwip_sendto / lwip_recvfrom /
// lwip_close call arguments for test assertions.
struct LwipSocketStubState {
    int  lastSocketDomain   = 0;
    int  lastSocketType     = 0;
    int  lastSocketProtocol = 0;
    int  socketReturnFd     = -1;  // override to make lwip_socket return a fd

    bool bindCalled         = false;
    int  bindFd             = -1;

    bool sendtoCalled       = false;
    int  sendtoFd           = -1;

    bool recvfromCalled     = false;
    int  recvfromFd         = -1;

    bool closeCalled        = false;
    int  closeFd            = -1;
};

inline LwipSocketStubState& lwipSocketStubState() {
    static LwipSocketStubState s;
    return s;
}

#include <cstdio>

inline int lwip_socket(int domain, int type, int protocol) {
    auto& s = lwipSocketStubState();
    s.lastSocketDomain   = domain;
    s.lastSocketType     = type;
    s.lastSocketProtocol = protocol;
    return s.socketReturnFd;
}

inline int lwip_bind(int fd, const struct sockaddr* addr, socklen_t len) {
    auto& s = lwipSocketStubState();
    s.bindCalled = true;
    s.bindFd     = fd;
    (void)addr; (void)len;
    return (fd >= 0) ? 0 : -1;
}

inline ssize_t lwip_sendto(int fd, const void* data, size_t size, int flags,
                           const struct sockaddr* to, socklen_t tolen) {
    auto& s = lwipSocketStubState();
    s.sendtoCalled = true;
    s.sendtoFd     = fd;
    (void)data; (void)size; (void)flags; (void)to; (void)tolen;
    return (fd >= 0) ? (ssize_t)size : -1;
}

inline ssize_t lwip_recvfrom(int fd, void* buf, size_t size, int flags,
                             struct sockaddr* from, socklen_t* fromlen) {
    auto& s = lwipSocketStubState();
    s.recvfromCalled = true;
    s.recvfromFd     = fd;
    (void)buf; (void)size; (void)flags; (void)from; (void)fromlen;
    return -1;  // no data by default
}

inline int lwip_close(int fd) {
    auto& s = lwipSocketStubState();
    s.closeCalled = true;
    s.closeFd     = fd;
    return 0;
}

// select() stub (used by upstream relay timeout)
inline int lwip_select(int nfds, fd_set* readfds, fd_set* writefds,
                       fd_set* exceptfds, struct timeval* timeout) {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout;
    return 0;  // no fd ready by default (timeout)
}

#ifndef select
#define select lwip_select
#endif
