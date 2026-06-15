#ifndef NETWORK_IO_H
#define NETWORK_IO_H

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <Mstcpip.h>
#include <BaseTsd.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef SSIZE_T nw_ssize_t;

typedef enum nw_socket_kind {
    NW_SOCKET_TCP,
    NW_SOCKET_UDP,
    NW_SOCKET_RAW
} nw_socket_kind_t;

typedef struct nw_socket_config {
    int af;
    int type;
    int protocol;
    unsigned long nonblocking;
    DWORD recv_timeout_ms;
    DWORD send_timeout_ms;
    int reuse_address;
} nw_socket_config_t;

typedef struct nw_endpoint {
    char host[64];
    unsigned short port;
} nw_endpoint_t;

int nw_init_winsock(void);
void nw_cleanup_winsock(void);

SOCKET nw_create_socket(const nw_socket_config_t *config);
SOCKET nw_create_tcp_server(unsigned short port, int backlog);
SOCKET nw_create_tcp_client(const char *host, unsigned short port);
SOCKET nw_create_udp_socket(unsigned short local_port);
SOCKET nw_create_raw_ipv4_socket(int protocol);

int nw_bind_socket(SOCKET socket_handle, const char *host, unsigned short port);
int nw_listen_socket(SOCKET socket_handle, int backlog);
SOCKET nw_accept_socket(SOCKET server_socket, nw_endpoint_t *client_endpoint);
int nw_connect_socket(SOCKET socket_handle, const char *host, unsigned short port);
int nw_resolve_ipv4(const char *host, char *output, size_t output_size);

nw_ssize_t nw_send_all(SOCKET socket_handle, const void *buffer, size_t length);
nw_ssize_t nw_recv_some(SOCKET socket_handle, void *buffer, size_t length);
nw_ssize_t nw_recv_exact(SOCKET socket_handle, void *buffer, size_t length);
nw_ssize_t nw_send_raw_bytes(
    SOCKET socket_handle,
    const void *buffer,
    size_t length,
    const char *host
);
nw_ssize_t nw_recv_raw_bytes(
    SOCKET socket_handle,
    void *buffer,
    size_t length,
    nw_endpoint_t *remote_endpoint
);
nw_ssize_t nw_sendto_endpoint(
    SOCKET socket_handle,
    const void *buffer,
    size_t length,
    const char *host,
    unsigned short port
);
nw_ssize_t nw_recvfrom_endpoint(
    SOCKET socket_handle,
    void *buffer,
    size_t length,
    nw_endpoint_t *remote_endpoint
);

int nw_set_nonblocking(SOCKET socket_handle, unsigned long enabled);
int nw_set_socket_timeout(SOCKET socket_handle, DWORD recv_timeout_ms, DWORD send_timeout_ms);
int nw_set_ip_header_included(SOCKET socket_handle, int enabled);
int nw_set_promiscuous_mode(SOCKET socket_handle, const char *local_ipv4);
void nw_close_socket(SOCKET socket_handle);

int nw_last_error(void);
const char *nw_error_string(int error_code);

#ifdef __cplusplus
}
#endif

#endif
