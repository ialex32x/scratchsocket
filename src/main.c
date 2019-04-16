#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

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

#define BUF_SIZE 1024
struct duk_sock_t {
#if WIN32
    SOCKET fd;
#else
    int fd;
#endif
    struct fd_set read_fds;
    struct fd_set write_fds;
    struct timeval timeout;
    char buf[BUF_SIZE];
};

void duk_sock_setnonblocking(struct duk_sock_t *sock) {
#if WIN32
    u_long argp = 0;
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
    duk_sock_setnonblocking(sock);
    return sock;
}

int duk_sock_connect(struct duk_sock_t *sock, struct sockaddr *addr, int port) {
    switch (addr->sa_family) {
        case AF_INET: {
            struct sockaddr_in sa;
            memcpy(&sa, addr, sizeof(struct sockaddr_in));
            sa.sin_port = htons(port);   
            return connect(sock->fd, (const struct sockaddr *)&sa, sizeof(struct sockaddr_in));
        }
        case AF_INET6: {
            struct sockaddr_in6 sa;
            memcpy(&sa, addr, sizeof(struct sockaddr_in6));
            sa.sin6_port = htons(port);   
            return connect(sock->fd, (const struct sockaddr *)&sa, sizeof(struct sockaddr_in6));
        }
        default: return -1;
    }
}

int duk_sock_close(struct duk_sock_t *sock) {
    closesocket(sock->fd);
    return 0;
}

int duk_sock_poll(struct duk_sock_t *sock) {
    FD_ZERO(&sock->read_fds);
    FD_ZERO(&sock->write_fds);
    FD_SET(sock->fd, &sock->read_fds);
    FD_SET(sock->fd, &sock->write_fds);
#if WIN32
    int maxfd = 0;
#else 
    int maxfd = sock->fd + 1;
#endif
    int res = select(maxfd, &sock->read_fds, &sock->write_fds, NULL, &sock->timeout);
    if (res < 0) {
        // error
        return -1;
    }
    if (res == 0) { 
        // timeout
        return 0;
    }
    if (FD_ISSET(sock->fd, &sock->read_fds)) {
        res = recv(sock->fd, sock->buf, BUF_SIZE, 0);
        if (res <= 0) {
            printf("recv failed %d (%d)\n", res, WSAGetLastError());
            return -1;
        }
        printf("echo %s\n", sock->buf);
    }
    if (FD_ISSET(sock->fd, &sock->write_fds)) {
        static int tick = 0;
        if (!(tick++ % 100)) {
            static char sendbuf[1024];
            int sendbufcap = 1024;
            int sendbuflen = 0;
            int sendcount = 0;
            sendbuflen = sprintf(sendbuf, "message#%d", sendcount++);
            res = send(sock->fd, sendbuf, sendbuflen, 0);
            if (res <= 0) {
                printf("send failed %d (%d)\n", res, WSAGetLastError());
                return -1;
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (!init()) {
        return 0;
    }

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
        struct duk_sock_t *sock = duk_sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        res = duk_sock_connect(sock, result, 1234);

        if (res < 0) {
            printf("connect failed %d (%d)\n", res, WSAGetLastError());
        } else {
            while (1) {
                res = duk_sock_poll(sock);
                if (res < 0) {
                    // error
                    break;
                }
                if (res == 0) { 
                    // timeout
                    continue;
                }
                Sleep(10);
            }
        }
        duk_sock_close(sock);
    }
    freeaddrinfo(resolved);
    deinit();
    system("pause");
    return 0;
}
