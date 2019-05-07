#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
// https://docs.microsoft.com/en-us/windows/desktop/WinSock/windows-sockets-error-codes-2

#include <process.h>

#include "ikcp.h"

int init() {
	WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 0);
    int err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) return 0;
    if ((LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 0) &&
        (LOBYTE(wsaData.wVersion) != 1 || HIBYTE(wsaData.wVersion) != 1)) {
        WSACleanup();
        return 0;
    }
    return 1;
}

void deinit() {
	WSACleanup();
}

enum duk_sock_state {
    DSOCK_CLOSED = 0,
    DSOCK_CONNECTING = 1,
    DSOCK_CONNECTED = 2,
};

#define BUF_SIZE 1024
struct duk_sock_t {
#if WIN32
    SOCKET fd;
#else
    int fd;
#endif
    enum duk_sock_state state;
    struct fd_set read_fds;
    struct fd_set write_fds;
    struct timeval timeout;
    char buf[BUF_SIZE];
    ikcpcb *kcp;
};

void duk_sock_setnonblocking(struct duk_sock_t *sock) {
#if WIN32
    u_long argp = 1;
    ioctlsocket(sock->fd, FIONBIO, &argp);
#else 
    int flags = fcntl(sock->fd, F_GETFL, 0);
    flags |= O_NONBLOCK;
    fcntl(sock->fd, F_SETFL, flags);
#endif
}

struct duk_sock_t *duk_sock_create(int af, int type, int protocol) {
#if WIN32
    SOCKET fd = socket(af, type, protocol);
    if (fd == INVALID_SOCKET) {
        return NULL;
    }
#else 
    int fd = socket(af, type, protocol);
    if (fd < 0) {
        return NULL;
    }
#endif
    struct duk_sock_t *sock = (struct duk_sock_t *)malloc(sizeof(struct duk_sock_t));
    memset(sock, 0, sizeof(struct duk_sock_t));
    sock->fd = fd;
    return sock;
}

int duk_sock_connect(struct duk_sock_t *sock, struct sockaddr *addr, int port) {
    int res = -1;
    switch (addr->sa_family) {
        case AF_INET: {
            struct sockaddr_in sa;
            memcpy(&sa, addr, sizeof(struct sockaddr_in));
            sa.sin_port = htons(port);
            sock->state = DSOCK_CONNECTING;
            res = connect(sock->fd, (const struct sockaddr *)&sa, sizeof(struct sockaddr_in));
            break;
        }
        case AF_INET6: {
            struct sockaddr_in6 sa;
            memcpy(&sa, addr, sizeof(struct sockaddr_in6));
            sa.sin6_port = htons(port);
            sock->state = DSOCK_CONNECTING;
            res = connect(sock->fd, (const struct sockaddr *)&sa, sizeof(struct sockaddr_in6));
            break;
        }
        default: return -1;
    }
    if (res < 0) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            printf("connect failed %d (%d)\n", res, WSAGetLastError());
            return -1;
        }
        res = 0;
    }
    // int optval;
    // socklen_t optlen = sizeof(optval);
    // res = getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (char *)&optval, &optlen);
    // if (res < 0) {
    //     printf("getsockopt failed %d (%d)\n", res, WSAGetLastError());
    //     return -1;
    // }
    return res;
}

int duk_sock_close(struct duk_sock_t *sock) {
    closesocket(sock->fd);
    return 0;
}

int duk_sock_poll(struct duk_sock_t *sock) {
    if (sock->state == DSOCK_CLOSED) {
        return 0;
    }
    int res;

    // if (sock->state == DSOCK_CONNECTING) {
    //     int optval;
    //     socklen_t optlen = sizeof(optval);
    //     res = getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, (char *)&optval, &optlen);
    //     if (res < 0) {
    //         printf("getsockopt failed %d (%d)\n", res, WSAGetLastError());
    //         return -1;
    //     }
    //     printf("getsockopt: %d\n", optval);
    //     sock->state = DSOCK_CONNECTED;
    //     return 0;
    // }

    FD_ZERO(&sock->read_fds);
    FD_ZERO(&sock->write_fds);
    FD_SET(sock->fd, &sock->read_fds);
    FD_SET(sock->fd, &sock->write_fds);
#if WIN32
    int maxfd = 0;
#else 
    int maxfd = sock->fd + 1;
#endif
    res = select(maxfd, &sock->read_fds, &sock->write_fds, NULL, &sock->timeout);
    if (res < 0) {
        // error
        sock->state = DSOCK_CLOSED;
        printf("select failed %d (%d)\n", res, WSAGetLastError());
        return -1;
    }
    if (res == 0) { 
        // timeout
        return 0;
    }

    if (FD_ISSET(sock->fd, &sock->read_fds)) {
        res = recv(sock->fd, sock->buf, BUF_SIZE, 0);
        if (res <= 0) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                sock->state = DSOCK_CLOSED;
                printf("recv failed %d (%d)\n", res, WSAGetLastError());
                return -1;
            }
        } else {
            sock->state = DSOCK_CONNECTED;
            if (sock->kcp) {
                res = ikcp_input(sock->kcp, sock->buf, res);
                if (res < 0) {
                    sock->state = DSOCK_CLOSED;
                    printf("kcp input failed %d\n", res);
                    return -1;
                }
                res = ikcp_recv(sock->kcp, sock->buf, BUF_SIZE);
                if (res > 0) {
                    sock->buf[res] = 0;
                    printf("echo %s\n", sock->buf);
                }
            } else {
                printf("echo %s\n", sock->buf);
            }
        }
    }

    if (FD_ISSET(sock->fd, &sock->write_fds)) {
        static int tick = 0;
        if (!(tick++ % 100)) {
            static char sendbuf[1024];
            int sendbuflen = 0;
            static int sendcount = 0;
            sendbuflen = sprintf(sendbuf, "message#%d", sendcount++);
            if (sock->kcp) {
                res = ikcp_send(sock->kcp, sendbuf, sendbuflen);
                if (res < 0) {
                    sock->state = DSOCK_CLOSED;
                    printf("kcp send failed %d\n", res);
                    return -1;
                }
            } else {
                res = send(sock->fd, sendbuf, sendbuflen, 0);
                if (res <= 0) {
                    int err = WSAGetLastError();
                    if (err != WSAEWOULDBLOCK) {
                        sock->state = DSOCK_CLOSED;
                        printf("send failed %d (%d)\n", res, WSAGetLastError());
                        return -1;
                    }
                }
            }
            sock->state = DSOCK_CONNECTED;
        }
    }
    return 0;
}

// unsigned _thread_work(void *ctx) {
//     return 0;
// }

int kcp_output(const char *buf, int len, struct IKCPCB *kcp, void *user) {
    struct duk_sock_t *sock = (struct duk_sock_t *) user;
    send(sock->fd, buf, len, 0);
    return 0;
}

int main(int argc, char *argv[]) {
    if (!init()) {
        return 0;
    }
    // _beginthreadex(NULL, 0, _thread_work, NULL, CREATE_SUSPENDED, NULL);

    const char *host = argv[1];
    const char *serv = NULL;
    struct addrinfo *resolved, *iter;
    struct addrinfo hints;
    struct sockaddr *result = NULL;
    size_t result_len = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = 0;
    int res = getaddrinfo(host, NULL, &hints, &resolved);
    if (res) {
        printf("getaddrinfo = %d\n", res);
        // system("pause");
        return 0;
    }
    char hbuf[NI_MAXHOST];

    for (iter = resolved; iter; iter = iter->ai_next) {
        getnameinfo(iter->ai_addr, (socklen_t) iter->ai_addrlen,
            hbuf, (socklen_t) sizeof(hbuf),
            NULL, 0, 0);

        switch (iter->ai_family) {
            case AF_INET: printf("[ipv4] %s\n", hbuf); 
                if (!result) {
                    result = iter->ai_addr;
                    result_len = iter->ai_addrlen;
                }
                break;
            case AF_INET6: printf("[ipv6] %s\n", hbuf); break;
            default: printf("[unkown] %s\n", hbuf); break;
        }
        // printf("resolve to %s\n", hbuf);
    }

    if (result) {
        printf("create socket\n");
        struct duk_sock_t *sock = duk_sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        // struct duk_sock_t *sock = duk_sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sock->kcp = ikcp_create(0x11223344, (void*)sock);
        sock->kcp->output = kcp_output;
        ikcp_nodelay(sock->kcp, 1, 10, 0, 0);
        ikcp_wndsize(sock->kcp, 512, 512);
        duk_sock_setnonblocking(sock);
        res = duk_sock_connect(sock, result, 1234);

        IUINT32 ts = 0;
        if (res >= 0) {
            while (1) {
                ikcp_update(sock->kcp, ts);
                res = duk_sock_poll(sock);
                if (res < 0) {
                    // error
                    printf("poll error");
                    break;
                }
                Sleep(10);
                ts += 10;
                if (res == 0) { 
                    // timeout
                    continue;
                }
            }
        }
        ikcp_release(sock->kcp);
        sock->kcp = 0;
        duk_sock_close(sock);
    }
    freeaddrinfo(resolved);
    deinit();
    system("pause");
    return 0;
}
