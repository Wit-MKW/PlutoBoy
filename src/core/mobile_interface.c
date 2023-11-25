#include "../non_core/mobile.h"
#include <string.h>

#ifdef THREE_DS
#define MOBILE_CONFIG_PATH "sdmc:/libmobile_conf.bin"
#else
#define MOBILE_CONFIG_PATH "./libmobile_conf.bin"
#endif

#define MAX(A, B) ((A) > (B) ? A : (B))

union u_sockaddr {
    struct sockaddr sa;
    struct sockaddr_in sin;
#ifdef IPV6_OK
    struct sockaddr_in6 sin6;
#endif
};

struct s_mobile_user {
    struct mobile_adapter *adapter;
    uint32_t frame_counter;
    uint32_t timer_latch[MOBILE_MAX_TIMERS];
    struct {
        int fd;
        enum mobile_socktype type;
    } sockets[MOBILE_MAX_CONNECTIONS];
    uint8_t config[MOBILE_CONFIG_SIZE];
    char numbers[2][MOBILE_MAX_NUMBER_SIZE + 1];
    bool serial;
};

static struct s_mobile_user mobile_user;

static socklen_t MobileAddrConvert1(const struct mobile_addr *src, union u_sockaddr *dest) {
#ifdef IPV6_OK
    if (src->type == MOBILE_ADDRTYPE_IPV6) {
        const struct mobile_addr6 *a6 = (const struct mobile_addr6*)src;
        dest->sin6.sin6_family = AF_INET6;
        dest->sin6.sin6_port = htons(a6->port);
        memcpy(&dest->sin6.sin6_addr.s6_addr, a6->host, MOBILE_HOSTLEN_IPV6);
        return MAX(sizeof(dest->sin6), sizeof(dest->sa));
    }
#endif
    const struct mobile_addr4 *a4 = (const struct mobile_addr4*)src;
    dest->sin.sin_family = AF_INET;
    dest->sin.sin_port = htons(a4->port);
    memcpy(&dest->sin.sin_addr.s_addr, a4->host, MOBILE_HOSTLEN_IPV4);
#ifdef IPV6_OK
    return MAX(sizeof(dest->sin), sizeof(dest->sa));
#else
    return sizeof(*dest);
#endif
}

static void MobileAddrConvert2(const union u_sockaddr *src, struct mobile_addr *dest) {
#ifdef IPV6_OK
    if (src->sa.sa_family == AF_INET6) {
        struct mobile_addr6 *a6 = (struct mobile_addr6*)dest;
        a6->type = MOBILE_ADDRTYPE_IPV6;
        a6->port = ntohs(src->sin6.sin6_port);
        memcpy(a6->host, &src->sin6.sin6_addr.s6_addr, MOBILE_HOSTLEN_IPV6);
    } else
#endif
    {
        struct mobile_addr4 *a4 =  (struct mobile_addr4*)dest;
        a4->type = MOBILE_ADDRTYPE_IPV4;
        a4->port = ntohs(src->sin.sin_port);
        memcpy(a4->host, &src->sin.sin_addr.s_addr, MOBILE_HOSTLEN_IPV4);
    }
}

static void MobileDebugLog(void *user, const char *line) {
    (void)user;
    log_message(LOG_INFO, "%s\n", line);
}

static void MobileSerialDisable(void *user) {
    struct s_mobile_user *m_user = user;
    m_user->serial = false;
}

static void MobileSerialEnable(void *user, bool mode_32bit) {
    struct s_mobile_user *m_user = user;
    m_user->serial = !mode_32bit;
}

static bool MobileConfigRead(void *user, void *dest, uintptr_t offset, size_t size) {
    struct s_mobile_user *m_user = user;
    return memcpy(dest, m_user->config + offset, size) == dest;
}

static bool MobileConfigWrite(void *user, const void *src, uintptr_t offset, size_t size) {
    struct s_mobile_user *m_user = user;
    return memcpy(m_user->config + offset, src, size) == (m_user->config + offset);
}

static void MobileTimeLatch(void *user, unsigned timer) {
    struct s_mobile_user *m_user = user;
    m_user->timer_latch[timer] = m_user->frame_counter;
}

static bool MobileTimeCheckMs(void *user, unsigned timer, unsigned ms) {
    struct s_mobile_user *m_user = user;
    unsigned time = m_user->frame_counter - m_user->timer_latch[timer];
    return (time >> 3) * 125 >> 16 >= ms;
}

static bool MobileSockOpen(void *user, unsigned conn, enum mobile_socktype type,
    enum mobile_addrtype addrtype, unsigned bindport) {
    struct s_mobile_user *m_user = user;

    int fd = socket(addrtype == MOBILE_ADDRTYPE_IPV6 ? AF_INET6 : AF_INET,
        type == MOBILE_SOCKTYPE_UDP ? SOCK_DGRAM : SOCK_STREAM,
        type == MOBILE_SOCKTYPE_UDP ? IPPROTO_UDP : IPPROTO_TCP);
    if (fd == -1)
        return false;

#ifdef _WIN32
    u_long nblk = 1;
    ioctlsocket(fd, FIONBIO, &nblk);
#else
    int nblk = fcntl(fd, F_GETFL, 0);
    if (nblk != -1) fcntl(fd, F_SETFL, nblk | O_NONBLOCK);
#endif

    int flag = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    if (type != MOBILE_SOCKTYPE_UDP)
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    if (bindport) {
        union u_sockaddr u_addr;
        socklen_t addrlen = sizeof(u_addr);
        memset(&u_addr, 0, addrlen);

#ifdef IPV6_OK
        if (addrtype == MOBILE_ADDRTYPE_IPV6) {
            u_addr.sin6.sin6_family = AF_INET6;
            u_addr.sin6.sin6_port = htons(bindport);
            addrlen = MAX(sizeof(u_addr.sin6), sizeof(u_addr.sa));
        } else
#endif
        {
            u_addr.sin.sin_family = AF_INET;
            u_addr.sin.sin_port = htons(bindport);
#ifdef THREE_DS
            u_addr.sin.sin_addr.s_addr = gethostid();
#elif defined(IPV6_OK)
            addrlen = MAX(sizeof(u_addr.sin), sizeof(u_addr.sa));
#endif
        }

        if (bind(fd, &u_addr.sa, addrlen) == -1) {
            close(fd);
            return false;
        }
    }

    m_user->sockets[conn].fd = fd;
    m_user->sockets[conn].type = type;
    return true;
}

static void MobileSockClose(void *user, unsigned conn) {
    struct s_mobile_user *m_user = user;
    close(m_user->sockets[conn].fd);
    m_user->sockets[conn].fd = -1;
    m_user->sockets[conn].type = (enum mobile_socktype)(-1);
}

static int MobileSockConnect(void *user, unsigned conn, const struct mobile_addr *addr) {
    struct s_mobile_user *m_user = user;
    union u_sockaddr u_addr;
    memset(&u_addr, 0, sizeof(u_addr));
    socklen_t addrlen = MobileAddrConvert1(addr, &u_addr);

    if (connect(m_user->sockets[conn].fd, &u_addr.sa, addrlen) != -1)
        return 1;
    if (SOCKET_ERR(EISCONN)) return 1;

    if (SOCKET_ERR(EWOULDBLOCK) || SOCKET_ERR(EINPROGRESS) || SOCKET_ERR(EALREADY))
        return 0;

    return -1;
}

static bool MobileSockListen(void *user, unsigned conn) {
    struct s_mobile_user *m_user = user;
    return listen(m_user->sockets[conn].fd, 1) != -1;
}

static bool MobileSockAccept(void *user, unsigned conn) {
    struct s_mobile_user *m_user = user;
    int fd = accept(m_user->sockets[conn].fd, NULL, NULL);
    if (fd == -1) return false;

#ifdef _WIN32
    u_long nblk = 1;
    ioctlsocket(fd, FIONBIO, &nblk);
#else
    int nblk = fcntl(fd, F_GETFL, 0);
    if (nblk != -1) fcntl(fd, F_SETFL, nblk | O_NONBLOCK);
#endif

    close(m_user->sockets[conn].fd);
    m_user->sockets[conn].fd = fd;
    return true;
}

static int MobileSockSend(void *user, unsigned conn, const void *data,
    unsigned size, const struct mobile_addr *addr) {
    struct s_mobile_user *m_user = user;

    if (!addr) return (int)send(m_user->sockets[conn].fd, data, size, 0);

    union u_sockaddr u_addr;
    memset(&u_addr, 0, sizeof(u_addr));
    socklen_t addrlen = MobileAddrConvert1(addr, &u_addr);

    ssize_t res = sendto(m_user->sockets[conn].fd, data, size, 0, &u_addr.sa, addrlen);
    if (res == -1 && SOCKET_ERR(EWOULDBLOCK)) return 0;
    return (int)res;
}

static int MobileSockRecv(void *user, unsigned conn, void *data,
    unsigned size, struct mobile_addr *addr) {
    struct s_mobile_user *m_user = user;

    fd_set rfds, efds;
    FD_ZERO(&rfds);
    FD_ZERO(&efds);
    FD_SET(m_user->sockets[conn].fd, &rfds);
    FD_SET(m_user->sockets[conn].fd, &efds);
    struct timeval tv = {0};
    if (select(m_user->sockets[conn].fd + 1, &rfds, NULL, &efds, &tv) <= 0)
        return 0;

    union u_sockaddr u_addr;
    socklen_t addrlen = sizeof(u_addr);
    memset(&u_addr, 0, addrlen);

    ssize_t res;
    if (data) {
        res = recvfrom(m_user->sockets[conn].fd, data, size, 0, &u_addr.sa, &addrlen);
    } else {
        char tmp;
        res = recvfrom(m_user->sockets[conn].fd, &tmp, 1, MSG_PEEK, &u_addr.sa, &addrlen);
    }

    if (res == -1) return -1;

    if (!res && m_user->sockets[conn].type != MOBILE_SOCKTYPE_UDP)
        return -2;
    
    if (!data) return 0;

    if (addr) MobileAddrConvert2(&u_addr, addr);

    return res;
}

static void MobileUpdateNumber(void *user, enum mobile_number type, const char *number) {
    struct s_mobile_user *m_user = user;
    memset(m_user->numbers[type], 0, MOBILE_MAX_NUMBER_SIZE + 1);
    if (number) strncpy(m_user->numbers[type], number, MOBILE_MAX_NUMBER_SIZE);
}

int MobileInit(void) {
    if (mobile_user.adapter) return 1;
    SocketInit();
    memset(&mobile_user, 0, sizeof(mobile_user));
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; ++i) {
        mobile_user.sockets[i].fd = -1;
        mobile_user.sockets[i].type = (enum mobile_socktype)(-1);
    }
    load_SRAM(MOBILE_CONFIG_PATH, mobile_user.config, MOBILE_CONFIG_SIZE);

    mobile_user.adapter = mobile_new(&mobile_user);
    mobile_def_debug_log(mobile_user.adapter, MobileDebugLog);
    mobile_def_serial_disable(mobile_user.adapter, MobileSerialDisable);
    mobile_def_serial_enable(mobile_user.adapter, MobileSerialEnable);
    mobile_def_config_read(mobile_user.adapter, MobileConfigRead);
    mobile_def_config_write(mobile_user.adapter, MobileConfigWrite);
    mobile_def_time_latch(mobile_user.adapter, MobileTimeLatch);
    mobile_def_time_check_ms(mobile_user.adapter, MobileTimeCheckMs);
    mobile_def_sock_open(mobile_user.adapter, MobileSockOpen);
    mobile_def_sock_close(mobile_user.adapter, MobileSockClose);
    mobile_def_sock_connect(mobile_user.adapter, MobileSockConnect);
    mobile_def_sock_listen(mobile_user.adapter, MobileSockListen);
    mobile_def_sock_accept(mobile_user.adapter, MobileSockAccept);
    mobile_def_sock_send(mobile_user.adapter, MobileSockSend);
    mobile_def_sock_recv(mobile_user.adapter, MobileSockRecv);
    mobile_def_update_number(mobile_user.adapter, MobileUpdateNumber);

    enum mobile_adapter_device device;
    bool unmetered;
    struct mobile_addr dns1;
    struct mobile_addr dns2;
    struct mobile_addr relay;
    unsigned p2p_port;
    unsigned char token[MOBILE_RELAY_TOKEN_SIZE];
    bool token_set;

    mobile_config_load(mobile_user.adapter);
    mobile_config_get_device(mobile_user.adapter, &device, &unmetered);
    mobile_config_get_dns(mobile_user.adapter, &dns1, MOBILE_DNS1);
    mobile_config_get_dns(mobile_user.adapter, &dns2, MOBILE_DNS2);
    mobile_config_get_p2p_port(mobile_user.adapter, &p2p_port);
    mobile_config_get_relay(mobile_user.adapter, &relay);
    token_set = mobile_config_get_relay_token(mobile_user.adapter, token);

    if (!MobileConf(&device, &unmetered, &dns1, &dns2, &relay, &p2p_port, token, &token_set))
        return 0;
    mobile_config_set_device(mobile_user.adapter, device, unmetered);
    mobile_config_set_dns(mobile_user.adapter, &dns1, MOBILE_DNS1);
    mobile_config_set_dns(mobile_user.adapter, &dns2, MOBILE_DNS2);
    mobile_config_set_p2p_port(mobile_user.adapter, p2p_port);
    mobile_config_set_relay(mobile_user.adapter, &relay);
    mobile_config_set_relay_token(mobile_user.adapter, token_set ? token : NULL);
    mobile_start(mobile_user.adapter);
    return 1;
}

void MobileDeinit(void) {
    if (!mobile_user.adapter) return;
    mobile_stop(mobile_user.adapter);
    save_SRAM(MOBILE_CONFIG_PATH, mobile_user.config, MOBILE_CONFIG_SIZE);
    memset(&mobile_user, 0, sizeof(mobile_user));
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; ++i) {
        mobile_user.sockets[i].fd = -1;
        mobile_user.sockets[i].type = (enum mobile_socktype)(-1);
    }
    SocketDeinit();
}

void MobileLoop(unsigned cycles) {
    mobile_user.frame_counter += cycles;
    if ((!cycles || cycles >= 4096 || (mobile_user.frame_counter & 4095) < cycles) && mobile_user.adapter)
        mobile_loop(mobile_user.adapter);
}

uint8_t MobileTransfer(uint8_t data) {
    MobileLoop(0);
    if (mobile_user.serial)
        return mobile_transfer(mobile_user.adapter, data);
    return MOBILE_SERIAL_IDLE_BYTE;
}
