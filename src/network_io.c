#include "network_io.h"

#include <stdio.h>
#include <string.h>

static int copy_ipv4_string(const struct in_addr *address, char *output, size_t output_size)
{
    const char *text;

    if (address == NULL || output == NULL || output_size == 0) {
        return SOCKET_ERROR;
    }

    text = inet_ntoa(*address);
    if (text == NULL) {
        return SOCKET_ERROR;
    }

    strncpy(output, text, output_size - 1U);
    output[output_size - 1U] = '\0';
    return 0;
}

static int fill_sockaddr_in(struct sockaddr_in *address, const char *host, unsigned short port)
{
    unsigned long ipv4;

    if (address == NULL) {
        return SOCKET_ERROR;
    }

    address->sin_family = AF_INET;
    address->sin_port = htons(port);

    if (host == NULL || host[0] == '\0' || strcmp(host, "0.0.0.0") == 0) {
        address->sin_addr.s_addr = htonl(INADDR_ANY);
        return 0;
    }

    ipv4 = inet_addr(host);
    if (ipv4 == INADDR_NONE && strcmp(host, "255.255.255.255") != 0) {
        return SOCKET_ERROR;
    }

    address->sin_addr.s_addr = ipv4;
    return 0;
}

int nw_resolve_ipv4(const char *host, char *output, size_t output_size)
{
    struct sockaddr_in *ipv4_address;
    struct addrinfo hints;
    struct addrinfo *result;
    int status;

    if (host == NULL || output == NULL || output_size == 0) {
        return SOCKET_ERROR;
    }

    if (strcmp(host, "localhost") == 0) {
        strncpy(output, "127.0.0.1", output_size - 1U);
        output[output_size - 1U] = '\0';
        return 0;
    }

    if (inet_addr(host) != INADDR_NONE || strcmp(host, "255.255.255.255") == 0) {
        strncpy(output, host, output_size - 1U);
        output[output_size - 1U] = '\0';
        return 0;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_ICMP;

    result = NULL;
    status = getaddrinfo(host, NULL, &hints, &result);
    if (status != 0 || result == NULL) {
        return SOCKET_ERROR;
    }

    ipv4_address = (struct sockaddr_in *)result->ai_addr;
    status = copy_ipv4_string(&ipv4_address->sin_addr, output, output_size);
    freeaddrinfo(result);
    return status;
}

int nw_init_winsock(void)
{
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data);
}

void nw_cleanup_winsock(void)
{
    WSACleanup();
}

SOCKET nw_create_socket(const nw_socket_config_t *config)
{
    SOCKET socket_handle;

    if (config == NULL) {
        return INVALID_SOCKET;
    }

    socket_handle = socket(config->af, config->type, config->protocol);
    if (socket_handle == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    if (config->reuse_address != 0) {
        setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR, (const char *)&config->reuse_address, sizeof(config->reuse_address));
    }

    if (config->recv_timeout_ms != 0 || config->send_timeout_ms != 0) {
        nw_set_socket_timeout(socket_handle, config->recv_timeout_ms, config->send_timeout_ms);
    }

    if (config->nonblocking != 0) {
        nw_set_nonblocking(socket_handle, config->nonblocking);
    }

    return socket_handle;
}

SOCKET nw_create_tcp_server(unsigned short port, int backlog)
{
    nw_socket_config_t config;
    SOCKET server_socket;

    config.af = AF_INET;
    config.type = SOCK_STREAM;
    config.protocol = IPPROTO_TCP;
    config.nonblocking = 0;
    config.recv_timeout_ms = 0;
    config.send_timeout_ms = 0;
    config.reuse_address = 1;

    server_socket = nw_create_socket(&config);
    if (server_socket == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    if (nw_bind_socket(server_socket, NULL, port) != 0 || nw_listen_socket(server_socket, backlog) != 0) {
        nw_close_socket(server_socket);
        return INVALID_SOCKET;
    }

    return server_socket;
}

SOCKET nw_create_tcp_client(const char *host, unsigned short port)
{
    nw_socket_config_t config;
    SOCKET client_socket;

    config.af = AF_INET;
    config.type = SOCK_STREAM;
    config.protocol = IPPROTO_TCP;
    config.nonblocking = 0;
    config.recv_timeout_ms = 5000;
    config.send_timeout_ms = 5000;
    config.reuse_address = 0;

    client_socket = nw_create_socket(&config);
    if (client_socket == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    if (nw_connect_socket(client_socket, host, port) != 0) {
        nw_close_socket(client_socket);
        return INVALID_SOCKET;
    }

    return client_socket;
}

SOCKET nw_create_udp_socket(unsigned short local_port)
{
    nw_socket_config_t config;
    SOCKET socket_handle;

    config.af = AF_INET;
    config.type = SOCK_DGRAM;
    config.protocol = IPPROTO_UDP;
    config.nonblocking = 0;
    config.recv_timeout_ms = 0;
    config.send_timeout_ms = 0;
    config.reuse_address = 1;

    socket_handle = nw_create_socket(&config);
    if (socket_handle == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    if (local_port != 0 && nw_bind_socket(socket_handle, NULL, local_port) != 0) {
        nw_close_socket(socket_handle);
        return INVALID_SOCKET;
    }

    return socket_handle;
}

SOCKET nw_create_raw_ipv4_socket(int protocol)
{
    nw_socket_config_t config;

    config.af = AF_INET;
    config.type = SOCK_RAW;
    config.protocol = protocol;
    config.nonblocking = 0;
    config.recv_timeout_ms = 1000;
    config.send_timeout_ms = 1000;
    config.reuse_address = 0;

    return nw_create_socket(&config);
}

int nw_bind_socket(SOCKET socket_handle, const char *host, unsigned short port)
{
    struct sockaddr_in address;

    if (fill_sockaddr_in(&address, host, port) != 0) {
        return SOCKET_ERROR;
    }

    return bind(socket_handle, (const struct sockaddr *)&address, (int)sizeof(address));
}

int nw_listen_socket(SOCKET socket_handle, int backlog)
{
    return listen(socket_handle, backlog);
}

SOCKET nw_accept_socket(SOCKET server_socket, nw_endpoint_t *client_endpoint)
{
    struct sockaddr_in address;
    int address_length;
    SOCKET client_socket;

    address_length = (int)sizeof(address);
    client_socket = accept(server_socket, (struct sockaddr *)&address, &address_length);
    if (client_socket == INVALID_SOCKET || client_endpoint == NULL) {
        return client_socket;
    }

    copy_ipv4_string(&address.sin_addr, client_endpoint->host, sizeof(client_endpoint->host));
    client_endpoint->port = ntohs(address.sin_port);
    return client_socket;
}

int nw_connect_socket(SOCKET socket_handle, const char *host, unsigned short port)
{
    struct sockaddr_in address;

    if (fill_sockaddr_in(&address, host, port) != 0) {
        return SOCKET_ERROR;
    }

    return connect(socket_handle, (const struct sockaddr *)&address, (int)sizeof(address));
}

nw_ssize_t nw_send_all(SOCKET socket_handle, const void *buffer, size_t length)
{
    const char *cursor;
    size_t sent_total;
    int sent_now;

    cursor = (const char *)buffer;
    sent_total = 0;

    while (sent_total < length) {
        sent_now = send(socket_handle, cursor + sent_total, (int)(length - sent_total), 0);
        if (sent_now == SOCKET_ERROR) {
            return -1;
        }
        sent_total += (size_t)sent_now;
    }

    return (nw_ssize_t)sent_total;
}

nw_ssize_t nw_recv_some(SOCKET socket_handle, void *buffer, size_t length)
{
    return (nw_ssize_t)recv(socket_handle, (char *)buffer, (int)length, 0);
}

nw_ssize_t nw_recv_exact(SOCKET socket_handle, void *buffer, size_t length)
{
    char *cursor;
    size_t received_total;
    int received_now;

    cursor = (char *)buffer;
    received_total = 0;

    while (received_total < length) {
        received_now = recv(socket_handle, cursor + received_total, (int)(length - received_total), 0);
        if (received_now == SOCKET_ERROR || received_now == 0) {
            return -1;
        }
        received_total += (size_t)received_now;
    }

    return (nw_ssize_t)received_total;
}

nw_ssize_t nw_send_raw_bytes(
    SOCKET socket_handle,
    const void *buffer,
    size_t length,
    const char *host
)
{
    struct sockaddr_in address;

    if (fill_sockaddr_in(&address, host, 0) != 0) {
        return -1;
    }

    return (nw_ssize_t)sendto(
        socket_handle,
        (const char *)buffer,
        (int)length,
        0,
        (const struct sockaddr *)&address,
        (int)sizeof(address)
    );
}

nw_ssize_t nw_recv_raw_bytes(
    SOCKET socket_handle,
    void *buffer,
    size_t length,
    nw_endpoint_t *remote_endpoint
)
{
    return nw_recvfrom_endpoint(socket_handle, buffer, length, remote_endpoint);
}

nw_ssize_t nw_sendto_endpoint(
    SOCKET socket_handle,
    const void *buffer,
    size_t length,
    const char *host,
    unsigned short port
)
{
    struct sockaddr_in address;

    if (fill_sockaddr_in(&address, host, port) != 0) {
        return -1;
    }

    return (nw_ssize_t)sendto(
        socket_handle,
        (const char *)buffer,
        (int)length,
        0,
        (const struct sockaddr *)&address,
        (int)sizeof(address)
    );
}

nw_ssize_t nw_recvfrom_endpoint(
    SOCKET socket_handle,
    void *buffer,
    size_t length,
    nw_endpoint_t *remote_endpoint
)
{
    struct sockaddr_in address;
    int address_length;
    int received;

    address_length = (int)sizeof(address);
    received = recvfrom(
        socket_handle,
        (char *)buffer,
        (int)length,
        0,
        (struct sockaddr *)&address,
        &address_length
    );

    if (received != SOCKET_ERROR && remote_endpoint != NULL) {
        copy_ipv4_string(&address.sin_addr, remote_endpoint->host, sizeof(remote_endpoint->host));
        remote_endpoint->port = ntohs(address.sin_port);
    }

    return (nw_ssize_t)received;
}

int nw_set_nonblocking(SOCKET socket_handle, unsigned long enabled)
{
    return ioctlsocket(socket_handle, FIONBIO, &enabled);
}

int nw_set_socket_timeout(SOCKET socket_handle, DWORD recv_timeout_ms, DWORD send_timeout_ms)
{
    int status;

    status = 0;
    if (recv_timeout_ms != 0) {
        status = setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO, (const char *)&recv_timeout_ms, sizeof(recv_timeout_ms));
    }
    if (status == 0 && send_timeout_ms != 0) {
        status = setsockopt(socket_handle, SOL_SOCKET, SO_SNDTIMEO, (const char *)&send_timeout_ms, sizeof(send_timeout_ms));
    }
    return status;
}

int nw_set_ip_header_included(SOCKET socket_handle, int enabled)
{
    return setsockopt(
        socket_handle,
        IPPROTO_IP,
        IP_HDRINCL,
        (const char *)&enabled,
        sizeof(enabled)
    );
}

int nw_set_promiscuous_mode(SOCKET socket_handle, const char *local_ipv4)
{
    DWORD bytes_returned;
    struct sockaddr_in address;
    DWORD receive_all;

    receive_all = RCVALL_ON;
    bytes_returned = 0;

    if (fill_sockaddr_in(&address, local_ipv4, 0) != 0) {
        return SOCKET_ERROR;
    }

    if (bind(socket_handle, (const struct sockaddr *)&address, (int)sizeof(address)) == SOCKET_ERROR) {
        return SOCKET_ERROR;
    }

    return WSAIoctl(
        socket_handle,
        SIO_RCVALL,
        &receive_all,
        sizeof(receive_all),
        NULL,
        0,
        &bytes_returned,
        NULL,
        NULL
    );
}

void nw_close_socket(SOCKET socket_handle)
{
    if (socket_handle != INVALID_SOCKET) {
        closesocket(socket_handle);
    }
}

int nw_last_error(void)
{
    return WSAGetLastError();
}

const char *nw_error_string(int error_code)
{
    switch (error_code) {
        case WSAEACCES: return "permission denied";
        case WSAEADDRINUSE: return "address already in use";
        case WSAECONNRESET: return "connection reset";
        case WSAECONNREFUSED: return "connection refused";
        case WSAETIMEDOUT: return "operation timed out";
        case WSAEWOULDBLOCK: return "operation would block";
        default: return "winsock error";
    }
}
