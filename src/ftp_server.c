#include "ftp_app.h"

#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

#define FTP_DIR_RESPONSE_SIZE 1024

static int send_text_line(SOCKET socket_handle, const char *text)
{
    char buffer[1024];
    int written;

    written = snprintf(buffer, sizeof(buffer), "%s\n", text);
    if (written < 0) {
        return -1;
    }

    return (nw_send_all(socket_handle, buffer, (size_t)written) < 0) ? -1 : 0;
}

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

static int handle_pwd(SOCKET client_socket)
{
    char current_dir[FTP_MAX_PATH_LEN];

    if (_getcwd(current_dir, (int)sizeof(current_dir)) == NULL) {
        return send_text_line(client_socket, "ERR failed to get current directory");
    }
    return send_text_line(client_socket, current_dir);
}

static int handle_dir(SOCKET client_socket)
{
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char response[FTP_DIR_RESPONSE_SIZE];
    size_t used;

    memset(response, 0, sizeof(response));
    strncpy(response, "DIR ", sizeof(response) - 1U);
    used = strlen(response);

    find_handle = FindFirstFileA(".\\*", &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
        return send_text_line(client_socket, "ERR failed to list directory");
    }

    do {
        size_t name_length;

        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        name_length = strlen(find_data.cFileName);
        if (used + name_length + 4U >= sizeof(response)) {
            strncpy(response + used, "...", sizeof(response) - used - 1U);
            break;
        }

        strncpy(response + used, find_data.cFileName, sizeof(response) - used - 1U);
        used += name_length;

        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            strncpy(response + used, "/", sizeof(response) - used - 1U);
            used += 1U;
        }

        strncpy(response + used, " ", sizeof(response) - used - 1U);
        used += 1U;
    } while (FindNextFileA(find_handle, &find_data) != 0);

    FindClose(find_handle);

    if (used == 4U) {
        return send_text_line(client_socket, "DIR <empty>");
    }

    return send_text_line(client_socket, response);
}

static int handle_get(SOCKET client_socket, const char *path)
{
    FILE *file;
    uint8_t buffer[FTP_DATA_CHUNK_SIZE];
    ftp_data_header_t header;
    size_t read_size;
    uint32_t sequence_number;

    if (path == NULL || path[0] == '\0') {
        return send_text_line(client_socket, "ERR missing file name, usage: get <file>");
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return send_text_line(client_socket, "ERR file open failed");
    }

    if (send_text_line(client_socket, "OK BEGIN GET") != 0) {
        fclose(file);
        return -1;
    }

    sequence_number = 0;
    for (;;) {
        read_size = fread(buffer, 1, sizeof(buffer), file);
        memset(&header, 0, sizeof(header));
        header.sequence_number = htonl(sequence_number++);
        header.payload_length = htonl((uint32_t)read_size);
        header.is_last_chunk = feof(file) ? 1U : 0U;

        if (nw_send_all(client_socket, &header, sizeof(header)) < 0 ||
            nw_send_all(client_socket, buffer, read_size) < 0) {
            fclose(file);
            return -1;
        }

        if (header.is_last_chunk != 0U) {
            break;
        }
    }

    fclose(file);
    return 0;
}

static int handle_put(SOCKET client_socket, const char *path)
{
    FILE *file;
    ftp_data_header_t header;
    uint8_t buffer[FTP_DATA_CHUNK_SIZE];
    nw_ssize_t received;
    uint32_t payload_length;

    if (path == NULL || path[0] == '\0') {
        return send_text_line(client_socket, "ERR missing file name, usage: put <file>");
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        return send_text_line(client_socket, "ERR file create failed");
    }

    if (send_text_line(client_socket, "OK BEGIN PUT") != 0) {
        fclose(file);
        return -1;
    }

    for (;;) {
        received = nw_recv_exact(client_socket, &header, sizeof(header));
        if (received != (nw_ssize_t)sizeof(header)) {
            fclose(file);
            return -1;
        }

        payload_length = ntohl(header.payload_length);
        if (payload_length > sizeof(buffer)) {
            fclose(file);
            return -1;
        }

        if (payload_length > 0) {
            received = nw_recv_exact(client_socket, buffer, payload_length);
            if (received != (nw_ssize_t)payload_length) {
                fclose(file);
                return -1;
            }
            fwrite(buffer, 1, payload_length, file);
        }

        if (header.is_last_chunk != 0U) {
            break;
        }
    }

    fclose(file);
    return send_text_line(client_socket, "OK PUT COMPLETE");
}

ftp_command_type_t ftp_parse_command(const char *line, ftp_command_t *command)
{
    char local_buffer[FTP_MAX_COMMAND_LEN];
    char *token;

    if (line == NULL || command == NULL) {
        return FTP_CMD_INVALID;
    }

    memset(command, 0, sizeof(*command));
    strncpy(local_buffer, line, sizeof(local_buffer) - 1U);
    local_buffer[sizeof(local_buffer) - 1U] = '\0';

    token = strtok(local_buffer, " \t");
    if (token == NULL) {
        return FTP_CMD_INVALID;
    }

    if (_stricmp(token, "help") == 0) {
        command->type = FTP_CMD_HELP;
    } else if (_stricmp(token, "pwd") == 0) {
        command->type = FTP_CMD_PWD;
    } else if (_stricmp(token, "dir") == 0 || _stricmp(token, "ls") == 0) {
        command->type = FTP_CMD_DIR;
    } else if (_stricmp(token, "get") == 0) {
        command->type = FTP_CMD_GET;
    } else if (_stricmp(token, "put") == 0) {
        command->type = FTP_CMD_PUT;
    } else if (_stricmp(token, "quit") == 0 || _stricmp(token, "exit") == 0) {
        command->type = FTP_CMD_QUIT;
    } else {
        command->type = FTP_CMD_INVALID;
    }

    token = strtok(NULL, "");
    if (token != NULL) {
        strncpy(command->argument, token, sizeof(command->argument) - 1U);
    }

    return command->type;
}

static int handle_client_session(SOCKET client_socket)
{
    char line[FTP_MAX_COMMAND_LEN];
    ftp_command_t command;

    if (send_text_line(client_socket, "WELCOME custom ftp server") != 0) {
        return -1;
    }

    for (;;) {
        if (recv_line(client_socket, line, sizeof(line)) != 0) {
            return -1;
        }

        ftp_parse_command(line, &command);
        switch (command.type) {
            case FTP_CMD_HELP:
                if (send_text_line(client_socket, "COMMANDS: help pwd dir get <file> put <file> quit") != 0) {
                    return -1;
                }
                break;
            case FTP_CMD_PWD:
                if (handle_pwd(client_socket) != 0) {
                    return -1;
                }
                break;
            case FTP_CMD_DIR:
                if (handle_dir(client_socket) != 0) {
                    return -1;
                }
                break;
            case FTP_CMD_GET:
                if (handle_get(client_socket, command.argument) != 0) {
                    return -1;
                }
                break;
            case FTP_CMD_PUT:
                if (handle_put(client_socket, command.argument) != 0) {
                    return -1;
                }
                break;
            case FTP_CMD_QUIT:
                return send_text_line(client_socket, "BYE");
            case FTP_CMD_INVALID:
            default:
                if (send_text_line(client_socket, "ERR unknown command") != 0) {
                    return -1;
                }
                break;
        }
    }
}

int ftp_server_run(unsigned short port)
{
    SOCKET server_socket;
    SOCKET client_socket;
    nw_endpoint_t endpoint;
    int init_status;
    int result;

    init_status = nw_init_winsock();
    if (init_status != 0) {
        log_socket_error("WSAStartup", init_status);
        return 1;
    }

    server_socket = nw_create_tcp_server(port, 4);
    if (server_socket == INVALID_SOCKET) {
        log_socket_error("create server", nw_last_error());
        nw_cleanup_winsock();
        return 1;
    }

    log_message(LOG_LEVEL_INFO, "ftp server listening on port %u", (unsigned int)port);
    client_socket = nw_accept_socket(server_socket, &endpoint);
    if (client_socket == INVALID_SOCKET) {
        log_socket_error("accept", nw_last_error());
        nw_close_socket(server_socket);
        nw_cleanup_winsock();
        return 1;
    }

    log_message(LOG_LEVEL_INFO, "client connected from %s:%u", endpoint.host, endpoint.port);
    result = handle_client_session(client_socket);

    nw_close_socket(client_socket);
    nw_close_socket(server_socket);
    nw_cleanup_winsock();
    return (result == 0) ? 0 : 1;
}
