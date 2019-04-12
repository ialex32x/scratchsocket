#include <stdio.h>

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
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        u_long argp = 0;
        ioctlsocket(sock, FIONBIO, &argp);
        struct sockaddr_in sa;
        memcpy(&sa, result, sizeof(struct sockaddr_in));
        sa.sin_port = htons(1234);

        char sendbuf[1024];
        int sendbufcap = 1024;
        int sendbuflen = 0;
        int sendcount = 0;

        res = connect(sock, &sa, sizeof(struct sockaddr_in));
        if (res < 0) {
            printf("connect failed %d (%d)\n", res, WSAGetLastError());
        } else {
            while (1) {
                sendbuflen = sprintf(sendbuf, "message#%d", sendcount++);
                res = send(sock, sendbuf, sendbuflen, 0);
                if (res <= 0) {
                    printf("send failed %d (%d)\n", res, WSAGetLastError());
                    break;
                }
                res = recv(sock, sendbuf, sendbufcap, 0);
                if (res <= 0) {
                    printf("recv failed %d (%d)\n", res, WSAGetLastError());
                    break;
                }
                printf("echo %s\n", sendbuf);
                Sleep(1000);
            }
        }
        closesocket(sock);
    }
    freeaddrinfo(resolved);
    deinit();
    system("pause");
    return 0;
}
