#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

void init() {
	WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 0);
    int err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) return 0;
    if ((LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 0) &&
        (LOBYTE(wsaData.wVersion) != 1 || HIBYTE(wsaData.wVersion) != 1)) {
        WSACleanup();
        return 0;
    }
}

void deinit() {
	WSACleanup();
}

int main(int argc, char *argv[]) {
    init();

    int i;
    const char *host = argv[1];
    const char *serv = NULL;
    struct addrinfo *resolved, *iter;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = 0;
    int res = getaddrinfo(host, NULL, &hints, &resolved);
    if (res) {
        printf("getaddrinfo = %d\n", res);
        system("pause");
        return 0;
    }
    char hbuf[NI_MAXHOST];
    char sbuf[NI_MAXSERV];

    for (i = 1, iter = resolved; iter; i++, iter = iter->ai_next) {
        getnameinfo(iter->ai_addr, (socklen_t) iter->ai_addrlen,
            hbuf, (socklen_t) sizeof(hbuf),
            NULL, 0, 0);

        switch (iter->ai_family) {
            case AF_INET: printf("[ipv4] %s\n", hbuf); break;
            case AF_INET6: printf("[ipv6] %s\n", hbuf); break;
            default: printf("[unkown] %s\n", hbuf); break;
        }
        // printf("resolve to %s\n", hbuf);
    }
    freeaddrinfo(resolved);

    // SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	// u_long argp = 0;
    // ioctlsocket(sock, FIONBIO, &argp);
    // struct sockaddr_in sa;
    // memset(&sa, 0, sizeof(sa));
    // sa.sin_family = AF_INET;
    // sa.sin_port = htons(6500);

    // connect(sock, &sa, sizeof(sa));
	// while (1) {
	// 	// printf("loop\n");
	// 	Sleep(1000);
	// }
	// closesocket(sock);
    deinit();
    system("pause");
    return 0;
}
