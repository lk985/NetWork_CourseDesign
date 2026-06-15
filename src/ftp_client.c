#include "ftp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

static int recv_line(SOCKET socket_handle, char *buffer, size_t buffer_size)
{
    size_t used;
    char ch;
    nw_ssize_t received;

    used = 0;
    while (used + 1 < buffer_size) {
        received = nw_recv_some(socket_handle, &ch, 1);
        if (received <= 0) {
            return -1;
        }
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            buffer[used++] = ch;
        }
    }
    buffer[used] = '\0';
    return 0;
}

static int send_line(SOCKET socket_handle, const char *line)
{
    char buffer[FTP_MAX_COMMAND_LEN];
    int written;

    written = snprintf(buffer, sizeof(buffer), "%s\n", line);
    if (written < 0) {
        return -1;
    }
    return (nw_send_all(socket_handle, buffer, (size_t)written) < 0) ? -1 : 0;
}

static int handle_get(SOCKET socket_handle, const char *remote_path)
{
    char command_line[FTP_MAX_COMMAND_LEN];
    char response[FTP_MAX_COMMAND_LEN];
    FILE *file;
    ftp_data_header_t header;
    uint8_t buffer[FTP_DATA_CHUNK_SIZE];
    uint32_t payload_length;

    if (remote_path == NULL || remote_path[0] == '\0') {
        printf("usage: get <file>\n");
        return 0;
    }

    snprintf(command_line, sizeof(command_line), "get %s", remote_path);
    if (send_line(socket_handle, command_line) != 0 || recv_line(socket_handle, response, sizeof(response)) != 0) {
        return -1;
    }

    if (strncmp(response, "OK", 2) != 0) {
        printf("%s\n", response);
        return 0;
    }

    file = fopen(remote_path, "wb");
    if (file == NULL) {
        return -1;
    }

    for (;;) {
        if (nw_recv_exact(socket_handle, &header, sizeof(header)) != (nw_ssize_t)sizeof(header)) {
            fclose(file);
            return -1;
        }

        payload_length = ntohl(header.payload_length);
        if (payload_length > sizeof(buffer)) {
            fclose(file);
            return -1;
        }

        if (payload_length > 0 &&
            nw_recv_exact(socket_handle, buffer, payload_length) != (nw_ssize_t)payload_length) {
            fclose(file);
            return -1;
        }

        fwrite(buffer, 1, payload_length, file);
        if (header.is_last_chunk != 0U) {
            break;
        }
    }

    fclose(file);
    printf("download complete: %s\n", remote_path);
    return 0;
}

static int handle_put(SOCKET socket_handle, const char *local_path)
{
    char command_line[FTP_MAX_COMMAND_LEN];
    char response[FTP_MAX_COMMAND_LEN];
    FILE *file;
    ftp_data_header_t header;
    uint8_t buffer[FTP_DATA_CHUNK_SIZE];
    size_t read_size;
    uint32_t sequence_number;

    if (local_path == NULL || local_path[0] == '\0') {
        printf("usage: put <file>\n");
        return 0;
    }

    snprintf(command_line, sizeof(command_line), "put %s", local_path);
    if (send_line(socket_handle, command_line) != 0 || recv_line(socket_handle, response, sizeof(response)) != 0) {
        return -1;
    }

    if (strncmp(response, "OK", 2) != 0) {
        printf("%s\n", response);
        return 0;
    }

    file = fopen(local_path, "rb");
    if (file == NULL) {
        return -1;
    }

    sequence_number = 0;
    for (;;) {
        read_size = fread(buffer, 1, sizeof(buffer), file);
        memset(&header, 0, sizeof(header));
        header.sequence_number = htonl(sequence_number++);
        header.payload_length = htonl((uint32_t)read_size);
        header.is_last_chunk = feof(file) ? 1U : 0U;

        if (nw_send_all(socket_handle, &header, sizeof(header)) < 0 ||
            nw_send_all(socket_handle, buffer, read_size) < 0) {
            fclose(file);
            return -1;
        }

        if (header.is_last_chunk != 0U) {
            break;
        }
    }

    fclose(file);
    if (recv_line(socket_handle, response, sizeof(response)) != 0) {
        return -1;
    }
    printf("%s\n", response);
    return 0;
}

int ftp_client_run(const char *host, unsigned short port)
{
    SOCKET client_socket;
    char line[FTP_MAX_COMMAND_LEN];
    char response[FTP_MAX_COMMAND_LEN];
    int init_status;

    init_status = nw_init_winsock();
    if (init_status != 0) {
        log_socket_error("WSAStartup", init_status);
        return 1;
    }

    client_socket = nw_create_tcp_client(host, port);
    if (client_socket == INVALID_SOCKET) {
        log_socket_error("connect", nw_last_error());
        nw_cleanup_winsock();
        return 1;
    }

    if (recv_line(client_socket, response, sizeof(response)) == 0) {
        printf("%s\n", response);
    }

    printf("Type 'help' to view commands.\n");

    for (;;) {
        printf("ftp> ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        line[strcspn(line, "\r\n")] = '\0';

        if (_strnicmp(line, "get ", 4) == 0) {
            if (handle_get(client_socket, line + 4) != 0) {
                break;
            }
            continue;
        }

        if (_strnicmp(line, "put ", 4) == 0) {
            if (handle_put(client_socket, line + 4) != 0) {
                break;
            }
            continue;
        }

        if (send_line(client_socket, line) != 0 || recv_line(client_socket, response, sizeof(response)) != 0) {
            break;
        }
        printf("%s\n", response);

        if (_stricmp(line, "quit") == 0 || _stricmp(line, "exit") == 0) {
            break;
        }
    }

    nw_close_socket(client_socket);
    nw_cleanup_winsock();
    return 0;
}
