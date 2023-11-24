#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define SOCKET_ERR(E) (WSAGetLastError() == WSA ## E)
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#define SOCKET_ERR(E) (errno == E)
#endif

#ifdef THREE_DS
#include <3ds.h>
#include <malloc.h>
extern uint32_t *SOCUBuffer;
#elif defined(__SWITCH__)
#include <switch.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

static int socket_ready = 0;

static inline void SocketInit(void) {
    if (!socket_ready) {
#ifdef _WIN32
        WSAData data;
        WSAStartup(MAKEWORD(2, 2), &data);
#elif defined(THREE_DS)
        if (!SOCUBuffer) {
            SOCUBuffer = memalign(0x1000, 0x100000);
            socInit(SOCUBuffer, 0x100000);
        }
#elif defined(__SWITCH__)
        socketInitializeDefault();
#endif
    }
    socket_ready = 1;
}

static inline void SocketDeinit(void) {
    if (socket_ready) {
#ifdef _WIN32
        WSACleanup();
        socket_ready = 0;
#elif defined(THREE_DS)
        socExit();
        free(SOCUBuffer);
        SOCUBuffer = NULL;
        socket_ready = 0;
#elif defined(__SWITCH__)
        socketExit();
        socket_ready = 0;
#endif
    }
}
