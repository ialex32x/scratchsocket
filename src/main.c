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

/*
struct duk_kcp_t {
    struct duk_sock_t *sock;
    ikcpcb *kcp;
};
*/

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

#include <stdint.h>
typedef int32_t fix16_t;

static const fix16_t FOUR_DIV_PI  = 0x145F3;            /*!< Fix16 value of 4/PI */
static const fix16_t _FOUR_DIV_PI2 = 0xFFFF9840;        /*!< Fix16 value of -4/PI² */
static const fix16_t X4_CORRECTION_COMPONENT = 0x399A; 	/*!< Fix16 value of 0.225 */
static const fix16_t PI_DIV_4 = 0x0000C90F;             /*!< Fix16 value of PI/4 */
static const fix16_t THREE_PI_DIV_4 = 0x00025B2F;       /*!< Fix16 value of 3PI/4 */

static const fix16_t fix16_maximum  = 0x7FFFFFFF; /*!< the maximum value of fix16_t */
static const fix16_t fix16_minimum  = 0x80000000; /*!< the minimum value of fix16_t */
static const fix16_t fix16_overflow = 0x80000000; /*!< the value used to indicate overflows when FIXMATH_NO_OVERFLOW is not specified */

static const fix16_t fix16_pi  = 205887;     /*!< fix16_t value of pi */
static const fix16_t fix16_e   = 178145;     /*!< fix16_t value of e */
static const fix16_t fix16_one = 0x00010000; /*!< fix16_t value of 1 */

fix16_t fix16_mul(fix16_t inArg0, fix16_t inArg1) {
	// Each argument is divided to 16-bit parts.
	//					AB
	//			*	 CD
	// -----------
	//					BD	16 * 16 -> 32 bit products
	//				 CB
	//				 AD
	//				AC
	//			 |----| 64 bit product
	int32_t A = (inArg0 >> 16), C = (inArg1 >> 16);
	uint32_t B = (inArg0 & 0xFFFF), D = (inArg1 & 0xFFFF);
	
	int32_t AC = A*C;
	int32_t AD_CB = A*D + C*B;
	uint32_t BD = B*D;
	
	int32_t product_hi = AC + (AD_CB >> 16);
	
	// Handle carry from lower 32 bits to upper part of result.
	uint32_t ad_cb_temp = AD_CB << 16;
	uint32_t product_lo = BD + ad_cb_temp;
	if (product_lo < BD)
		product_hi++;
	
	// The upper 17 bits should all be the same (the sign).
	if (product_hi >> 31 != product_hi >> 15)
		return fix16_overflow;
	
#ifdef FIXMATH_NO_ROUNDING
	return (product_hi << 16) | (product_lo >> 16);
#else
	// Subtracting 0x8000 (= 0.5) and then using signed right shift
	// achieves proper rounding to result-1, except in the corner
	// case of negative numbers and lowest word = 0x8000.
	// To handle that, we also have to subtract 1 for negative numbers.
	uint32_t product_lo_tmp = product_lo;
	product_lo -= 0x8000;
	product_lo -= (uint32_t)product_hi >> 31;
	if (product_lo > product_lo_tmp)
		product_hi--;
	
	// Discard the lowest 16 bits. Note that this is not exactly the same
	// as dividing by 0x10000. For example if product = -1, result will
	// also be -1 and not 0. This is compensated by adding +1 to the result
	
    // and compensating this in turn in the rounding above.
	fix16_t result = (product_hi << 16) | (product_lo >> 16);
	result += 1;
	return result;
#endif
}

static inline fix16_t fix16_from_float(double a)
{
	double temp = a * fix16_one;
#ifndef FIXMATH_NO_ROUNDING
	temp += (temp >= 0) ? 0.5f : -0.5f;
#endif
	return (fix16_t)temp;
}
fix16_t fix16_div(fix16_t a, fix16_t b)
{
	// This uses the basic binary restoring division algorithm.
	// It appears to be faster to do the whole division manually than
	// trying to compose a 64-bit divide out of 32-bit divisions on
	// platforms without hardware divide.
	
	if (b == 0)
		return fix16_minimum;
	
	uint32_t remainder = (a >= 0) ? a : (-a);
	uint32_t divider = (b >= 0) ? b : (-b);

	uint32_t quotient = 0;
	uint32_t bit = 0x10000;
	
	/* The algorithm requires D >= R */
	while (divider < remainder)
	{
		divider <<= 1;
		bit <<= 1;
	}
	
	if (!bit)
		return fix16_overflow;
	
	if (divider & 0x80000000)
	{
		// Perform one step manually to avoid overflows later.
		// We know that divider's bottom bit is 0 here.
		if (remainder >= divider)
		{
				quotient |= bit;
				remainder -= divider;
		}
		divider >>= 1;
		bit >>= 1;
	}
	
	/* Main division loop */
	while (bit && remainder)
	{
		if (remainder >= divider)
		{
				quotient |= bit;
				remainder -= divider;
		}
		
		remainder <<= 1;
		bit >>= 1;
	}	 
			
	if (remainder >= divider)
	{
		quotient++;
	}
	
	fix16_t result = quotient;
	
	/* Figure out the sign of result */
	if ((a ^ b) & 0x80000000)
	{
		if (result == fix16_minimum)
				return fix16_overflow;
		
		result = -result;
	}
	
	return result;
}
static inline float   fix16_to_float(fix16_t a) { return (float)a / fix16_one; }

fix16_t fix16_sin(fix16_t inAngle)
{
	fix16_t tempAngle = inAngle % (fix16_pi << 1);

	if(tempAngle > fix16_pi)
		tempAngle -= (fix16_pi << 1);
	else if(tempAngle < -fix16_pi)
		tempAngle += (fix16_pi << 1);
    fix16_t tempOut;
	fix16_t tempAngleSq = fix16_mul(tempAngle, tempAngle);
	tempOut = fix16_mul(-13, tempAngleSq) + 546;
	tempOut = fix16_mul(tempOut, tempAngleSq) - 10923;
	tempOut = fix16_mul(tempOut, tempAngleSq) + 65536;
	tempOut = fix16_mul(tempOut, tempAngle);
    
	return tempOut;
}

fix16_t fix16_atan2(fix16_t inY , fix16_t inX)
{
	fix16_t abs_inY, mask, angle, r, r_3;

	/* Absolute inY */
	mask = (inY >> (sizeof(fix16_t)*CHAR_BIT-1));
	abs_inY = (inY + mask) ^ mask;

	if (inX >= 0)
	{
		r = fix16_div( (inX - abs_inY), (inX + abs_inY));
		r_3 = fix16_mul(fix16_mul(r, r),r);
		angle = fix16_mul(0x00003240 , r_3) - fix16_mul(0x0000FB50,r) + PI_DIV_4;
	} else {
		r = fix16_div( (inX + abs_inY), (abs_inY - inX));
		r_3 = fix16_mul(fix16_mul(r, r),r);
		angle = fix16_mul(0x00003240 , r_3)
			- fix16_mul(0x0000FB50,r)
			+ THREE_PI_DIV_4;
	}
	if (inY < 0)
	{
		angle = -angle;
	}

	return angle;
}

fix16_t fix16_atan(fix16_t x)
{
	return fix16_atan2(x, fix16_one);
}

int main(int argc, char *argv[]) {
    fix16_t a = fix16_from_float(-5.1);
    fix16_t c = fix16_atan(a);
    float fc = fix16_to_float(c);
    printf("c = %d (%f)\n", c, fc);
    return 0;
}

int d_main(int argc, char *argv[]) {
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
