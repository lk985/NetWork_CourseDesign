#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datalink_sim.h"
#include "ftp_app.h"
#include "network_io.h"
#include "protocol_structs.h"
#include "utils.h"

#include <iphlpapi.h>
#include <icmpapi.h>

static void print_usage(const char *program_name)
{
    printf("Usage:\n");
    printf("  %s help\n", program_name);
    printf("  %s ftp-server [port]    (default: %u)\n", program_name, (unsigned int)SERV_PORT);
    printf("  %s ftp-client <host> [port]    (default: %u)\n", program_name, (unsigned int)SERV_PORT);
    printf("  %s datalink-demo [stopwait|gbn|all] [loss_rate] [window] [timeout_ms] [input_file]\n", program_name);
    printf("  %s ping <host> [-t]\n", program_name);
    printf("  %s capture demo [tcp|udp|icmp]\n", program_name);
    printf("  %s capture <local-ip> [tcp|udp|icmp]\n", program_name);
}

static int parse_capture_protocol_filter(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    if (_stricmp(text, "tcp") == 0) {
        return IP_PROTO_TCP;
    }
    if (_stricmp(text, "udp") == 0) {
        return IP_PROTO_UDP;
    }
    if (_stricmp(text, "icmp") == 0) {
        return IP_PROTO_ICMP;
    }
    return -1;
}

static int packet_matches_protocol_filter(const parsed_packet_t *packet, int protocol_filter)
{
    if (packet == NULL || packet->ipv4 == NULL) {
        return 0;
    }
    if (protocol_filter == 0) {
        return 1;
    }
    return packet->ipv4->protocol == (uint8_t)protocol_filter;
}

static int parse_datalink_mode_text(const char *text, datalink_mode_t *mode)
{
    if (text == NULL || mode == NULL) {
        return -1;
    }

    if (_stricmp(text, "stopwait") == 0 || _stricmp(text, "stop-and-wait") == 0) {
        *mode = DLINK_MODE_STOP_AND_WAIT;
        return 0;
    }

    if (_stricmp(text, "gbn") == 0) {
        *mode = DLINK_MODE_GBN;
        return 0;
    }

    return -1;
}

static int queue_datalink_demo_samples(datalink_simulator_t *simulator)
{
    static const uint8_t sample1[] = "frame-01 alpha";
    static const uint8_t sample2[] = "frame-02 beta";
    static const uint8_t sample3[] = "frame-03 gamma";
    static const uint8_t sample4[] = "frame-04 delta";
    static const struct {
        const uint8_t *payload;
        size_t length;
    } samples[] = {
        { sample1, sizeof(sample1) - 1U },
        { sample2, sizeof(sample2) - 1U },
        { sample3, sizeof(sample3) - 1U },
        { sample4, sizeof(sample4) - 1U }
    };
    size_t sample_index;

    if (simulator == NULL) {
        return -1;
    }

    for (sample_index = 0; sample_index < sizeof(samples) / sizeof(samples[0]); ++sample_index) {
        if (datalink_simulator_queue_payload(
            simulator,
            samples[sample_index].payload,
            samples[sample_index].length
        ) != 0) {
            log_message(LOG_LEVEL_ERROR, "failed to queue datalink payload %lu", (unsigned long)sample_index);
            return -1;
        }
    }

    return 0;
}

static int queue_datalink_file_chunks(datalink_simulator_t *simulator, const char *file_path)
{
    FILE *file;
    uint8_t buffer[DLINK_MAX_PAYLOAD_SIZE];
    size_t chunk_index;
    size_t read_size;

    if (simulator == NULL || file_path == NULL || file_path[0] == '\0') {
        return -1;
    }

    file = fopen(file_path, "rb");
    if (file == NULL) {
        log_message(LOG_LEVEL_ERROR, "failed to open datalink input file: %s", file_path);
        return -1;
    }

    chunk_index = 0;
    for (;;) {
        read_size = fread(buffer, 1, sizeof(buffer), file);
        if (read_size > 0) {
            if (datalink_simulator_queue_payload(simulator, buffer, read_size) != 0) {
                log_message(LOG_LEVEL_ERROR, "failed to queue datalink file chunk %lu", (unsigned long)chunk_index);
                fclose(file);
                return -1;
            }
            chunk_index++;
        }

        if (read_size < sizeof(buffer)) {
            if (feof(file) != 0) {
                break;
            }
            log_message(LOG_LEVEL_ERROR, "failed while reading datalink input file: %s", file_path);
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    if (chunk_index == 0) {
        log_message(LOG_LEVEL_ERROR, "datalink input file is empty: %s", file_path);
        return -1;
    }

    printf("queued %lu chunks from file: %s\n", (unsigned long)chunk_index, file_path);
    return 0;
}

static int run_one_datalink_demo(
    datalink_mode_t mode,
    double loss_rate,
    size_t window_size,
    unsigned int timeout_ms,
    const char *input_file_path
)
{
    const char *mode_name;
    datalink_simulator_t *simulator;
    int status;

    mode_name = (mode == DLINK_MODE_GBN) ? "GBN" : "STOP_AND_WAIT";
    printf("=== datalink %s demo ===\n", mode_name);

    simulator = datalink_simulator_create(mode, window_size, timeout_ms, loss_rate);
    if (simulator == NULL) {
        log_message(LOG_LEVEL_ERROR, "failed to create datalink simulator");
        return 1;
    }

    if (input_file_path != NULL) {
        if (queue_datalink_file_chunks(simulator, input_file_path) != 0) {
            datalink_simulator_destroy(simulator);
            return 1;
        }
    } else if (queue_datalink_demo_samples(simulator) != 0) {
        datalink_simulator_destroy(simulator);
        return 1;
    }

    status = datalink_simulator_run(simulator);
    datalink_simulator_print_stats(simulator);
    datalink_simulator_destroy(simulator);
    printf("\n");
    return status;
}

static int run_datalink_demo(
    const char *mode_text,
    double loss_rate,
    size_t window_size,
    unsigned int timeout_ms,
    const char *input_file_path
)
{
    datalink_mode_t modes[] = { DLINK_MODE_STOP_AND_WAIT, DLINK_MODE_GBN };
    datalink_mode_t single_mode;
    int status;
    size_t mode_index;

    if (loss_rate < 0.0 || loss_rate > 1.0 || window_size == 0 || timeout_ms == 0U) {
        log_message(LOG_LEVEL_ERROR, "invalid datalink parameters");
        return 1;
    }

    if (mode_text != NULL && _stricmp(mode_text, "all") != 0) {
        if (parse_datalink_mode_text(mode_text, &single_mode) != 0) {
            log_message(LOG_LEVEL_ERROR, "unknown datalink mode: %s", mode_text);
            return 1;
        }
        return run_one_datalink_demo(single_mode, loss_rate, window_size, timeout_ms, input_file_path);
    }

    status = 0;
    for (mode_index = 0; mode_index < sizeof(modes) / sizeof(modes[0]); ++mode_index) {
        if (run_one_datalink_demo(modes[mode_index], loss_rate, window_size, timeout_ms, input_file_path) != 0) {
            status = 1;
        }
    }

    return status;
}

static int run_capture_demo(int protocol_filter)
{
    static const uint8_t tcp_payload[] = "HELLO_TCP_DEMO";
    static const uint8_t udp_payload[] = "HELLO_UDP_DEMO";
    static const uint8_t icmp_payload[] = "HELLO_ICMP_DEMO";
    uint8_t packet_buffer[sizeof(ethernet_header_t) + sizeof(ipv4_header_t) + sizeof(tcp_header_t) + 32U];
    ethernet_header_t *ethernet;
    ipv4_header_t *ipv4;
    tcp_header_t *tcp;
    udp_header_t *udp;
    icmp_header_t *icmp;
    uint8_t *payload;
    size_t payload_length;
    size_t transport_length;
    parsed_packet_t packet;
    packet_parse_result_t result;

    memset(packet_buffer, 0, sizeof(packet_buffer));

    ethernet = (ethernet_header_t *)packet_buffer;
    ethernet->destination[0] = 0x00;
    ethernet->destination[1] = 0x11;
    ethernet->destination[2] = 0x22;
    ethernet->destination[3] = 0x33;
    ethernet->destination[4] = 0x44;
    ethernet->destination[5] = 0x55;
    ethernet->source[0] = 0x66;
    ethernet->source[1] = 0x77;
    ethernet->source[2] = 0x88;
    ethernet->source[3] = 0x99;
    ethernet->source[4] = 0xAA;
    ethernet->source[5] = 0xBB;
    ethernet->ether_type = htons(ETHER_TYPE_IPV4);

    ipv4 = (ipv4_header_t *)(packet_buffer + sizeof(ethernet_header_t));
    ipv4->version_ihl = 0x45;
    ipv4->ttl = 64;
    ipv4->source_ip = inet_addr("192.168.1.10");
    ipv4->destination_ip = inet_addr("192.168.1.20");
    payload = packet_buffer + sizeof(ethernet_header_t) + sizeof(ipv4_header_t);

    if (protocol_filter == IP_PROTO_UDP) {
        ipv4->protocol = IP_PROTO_UDP;
        udp = (udp_header_t *)payload;
        udp->source_port = htons(5353);
        udp->destination_port = htons(8000);
        payload += sizeof(udp_header_t);
        memcpy(payload, udp_payload, sizeof(udp_payload) - 1U);
        payload_length = sizeof(udp_payload) - 1U;
        udp->length = htons((uint16_t)(sizeof(udp_header_t) + payload_length));
        transport_length = sizeof(udp_header_t) + payload_length;
    } else if (protocol_filter == IP_PROTO_ICMP) {
        ipv4->protocol = IP_PROTO_ICMP;
        icmp = (icmp_header_t *)payload;
        icmp->type = ICMP_TYPE_ECHO_REPLY;
        icmp->code = 0;
        icmp->identifier = htons(7);
        icmp->sequence_number = htons(3);
        payload += sizeof(icmp_header_t);
        memcpy(payload, icmp_payload, sizeof(icmp_payload) - 1U);
        payload_length = sizeof(icmp_payload) - 1U;
        transport_length = sizeof(icmp_header_t) + payload_length;
    } else {
        ipv4->protocol = IP_PROTO_TCP;
        tcp = (tcp_header_t *)payload;
        tcp->source_port = htons(12345);
        tcp->destination_port = htons(80);
        tcp->sequence_number = htonl(1001UL);
        tcp->acknowledgement_number = htonl(2002UL);
        tcp->data_offset_reserved = (uint8_t)(5U << 4);
        tcp->flags = (uint8_t)(TCP_FLAG_PSH | TCP_FLAG_ACK);
        tcp->window_size = htons(4096);
        payload += sizeof(tcp_header_t);
        memcpy(payload, tcp_payload, sizeof(tcp_payload) - 1U);
        payload_length = sizeof(tcp_payload) - 1U;
        transport_length = sizeof(tcp_header_t) + payload_length;
    }

    ipv4->total_length = htons((uint16_t)(sizeof(ipv4_header_t) + transport_length));

    result = parse_ethernet_ipv4_packet(
        packet_buffer,
        sizeof(ethernet_header_t) + sizeof(ipv4_header_t) + transport_length,
        &packet
    );
    if (result != PACKET_PARSE_OK) {
        log_message(LOG_LEVEL_ERROR, "capture demo parse failed, code=%d", (int)result);
        return 1;
    }

    printf("capture demo packet parsed successfully:\n");
    print_parsed_packet_summary(&packet);
    return 0;
}

static int run_capture_demo_all(void)
{
    int status;

    printf("=== TCP demo ===\n");
    status = run_capture_demo(IP_PROTO_TCP);
    if (status != 0) {
        return status;
    }

    printf("\n=== UDP demo ===\n");
    status = run_capture_demo(IP_PROTO_UDP);
    if (status != 0) {
        return status;
    }

    printf("\n=== ICMP demo ===\n");
    return run_capture_demo(IP_PROTO_ICMP);
}

static int run_live_capture(const char *local_ip, int protocol_filter)
{
    SOCKET capture_socket;
    uint8_t raw_packet[65535];
    uint8_t parse_buffer[sizeof(ethernet_header_t) + 65535];
    ethernet_header_t *ethernet;
    parsed_packet_t packet;
    packet_parse_result_t result;
    nw_endpoint_t remote_endpoint;
    nw_ssize_t received_length;
    int init_status;
    unsigned int attempts;

    init_status = nw_init_winsock();
    if (init_status != 0) {
        log_socket_error("WSAStartup", init_status);
        return 1;
    }

    capture_socket = nw_create_raw_ipv4_socket(IPPROTO_IP);
    if (capture_socket == INVALID_SOCKET) {
        log_socket_error("create capture socket", nw_last_error());
        log_message(LOG_LEVEL_WARN, "live capture usually requires administrator privileges on Windows");
        nw_cleanup_winsock();
        return 1;
    }

    if (nw_set_promiscuous_mode(capture_socket, local_ip) != 0) {
        log_socket_error("enable promiscuous mode", nw_last_error());
        nw_close_socket(capture_socket);
        nw_cleanup_winsock();
        return 1;
    }

    printf("capture listening on %s", local_ip);
    if (protocol_filter != 0) {
        printf(", filter=%s", ip_protocol_name((uint8_t)protocol_filter));
    }
    printf(", waiting for one matching IPv4 packet...\n");

    for (attempts = 0; attempts < 20U; ++attempts) {
        received_length = nw_recv_raw_bytes(capture_socket, raw_packet, sizeof(raw_packet), &remote_endpoint);
        if (received_length <= 0) {
            continue;
        }

        memset(parse_buffer, 0, sizeof(ethernet_header_t));
        ethernet = (ethernet_header_t *)parse_buffer;
        ethernet->ether_type = htons(ETHER_TYPE_IPV4);
        memcpy(parse_buffer + sizeof(ethernet_header_t), raw_packet, (size_t)received_length);

        result = parse_ethernet_ipv4_packet(
            parse_buffer,
            sizeof(ethernet_header_t) + (size_t)received_length,
            &packet
        );
        if (result != PACKET_PARSE_OK) {
            continue;
        }

        if (!packet_matches_protocol_filter(&packet, protocol_filter)) {
            continue;
        }

        printf("captured one packet from %s\n", remote_endpoint.host[0] != '\0' ? remote_endpoint.host : local_ip);
        print_parsed_packet_summary(&packet);
        nw_close_socket(capture_socket);
        nw_cleanup_winsock();
        return 0;
    }

    log_message(LOG_LEVEL_ERROR, "no matching packet captured within the current wait window");

    nw_close_socket(capture_socket);
    nw_cleanup_winsock();
    return 1;
}

static void fill_ping_payload(uint8_t *payload, size_t payload_length)
{
    size_t index;

    if (payload == NULL) {
        return;
    }

    for (index = 0; index < payload_length; ++index) {
        payload[index] = (uint8_t)('a' + (index % 26U));
    }
}

static int run_ping(const char *host, int continuous_mode)
{
    HANDLE icmp_handle;
    char resolved_ipv4[64];
    uint8_t payload[32];
    char reply_buffer[sizeof(ICMP_ECHO_REPLY) + 32 + 64];
    IPAddr destination_ip;
    PICMP_ECHO_REPLY echo_reply;
    int init_status;
    unsigned int sent_count;
    unsigned int received_count;
    int status;
    DWORD reply_count;

    init_status = nw_init_winsock();
    if (init_status != 0) {
        log_socket_error("WSAStartup", init_status);
        return 1;
    }

    if (nw_resolve_ipv4(host, resolved_ipv4, sizeof(resolved_ipv4)) != 0) {
        log_message(LOG_LEVEL_ERROR, "failed to resolve target host: %s", host);
        nw_cleanup_winsock();
        return 1;
    }

    icmp_handle = IcmpCreateFile();
    if (icmp_handle == INVALID_HANDLE_VALUE) {
        log_message(LOG_LEVEL_ERROR, "failed to create ICMP handle, error=%lu", (unsigned long)GetLastError());
        nw_cleanup_winsock();
        return 1;
    }

    destination_ip = inet_addr(resolved_ipv4);
    if (destination_ip == INADDR_NONE && strcmp(resolved_ipv4, "255.255.255.255") != 0) {
        log_message(LOG_LEVEL_ERROR, "invalid destination IPv4 address: %s", resolved_ipv4);
        IcmpCloseHandle(icmp_handle);
        nw_cleanup_winsock();
        return 1;
    }

    fill_ping_payload(payload, sizeof(payload));
    sent_count = 0;
    received_count = 0;
    status = 0;

    printf("Pinging %s [%s] with %u bytes of data:\n",
        host,
        resolved_ipv4,
        (unsigned int)sizeof(payload));

    for (;;) {
        reply_count = IcmpSendEcho(
            icmp_handle,
            destination_ip,
            (LPVOID)payload,
            (WORD)sizeof(payload),
            NULL,
            reply_buffer,
            (DWORD)sizeof(reply_buffer),
            1000
        );

        sent_count++;
        if (reply_count > 0) {
            echo_reply = (PICMP_ECHO_REPLY)reply_buffer;
            received_count++;
            printf(
                "Reply from %s: bytes=%u time=%lums TTL=%u\n",
                resolved_ipv4,
                (unsigned int)echo_reply->DataSize,
                (unsigned long)echo_reply->RoundTripTime,
                (unsigned int)echo_reply->Options.Ttl
            );
        } else {
            printf("Request timed out.\n");
        }

        if (!continuous_mode && sent_count >= 4U) {
            break;
        }

        Sleep(1000);
    }

    printf("\nPing statistics for %s:\n", resolved_ipv4);
    printf("    Packets: Sent = %u, Received = %u, Lost = %u\n",
        sent_count,
        received_count,
        sent_count - received_count);

    IcmpCloseHandle(icmp_handle);
    nw_cleanup_winsock();
    return status;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "ftp-server") == 0) {
        unsigned short port;

        if (argc != 2 && argc != 3) {
            print_usage(argv[0]);
            return 1;
        }

        port = (argc == 3) ? (unsigned short)atoi(argv[2]) : (unsigned short)SERV_PORT;
        return ftp_server_run(port);
    }

    if (strcmp(argv[1], "ftp-client") == 0) {
        unsigned short port;

        if (argc != 3 && argc != 4) {
            print_usage(argv[0]);
            return 1;
        }

        port = (argc == 4) ? (unsigned short)atoi(argv[3]) : (unsigned short)SERV_PORT;
        return ftp_client_run(argv[2], port);
    }

    if (strcmp(argv[1], "datalink-demo") == 0) {
        const char *mode_text;
        const char *input_file_path;
        double loss_rate;
        size_t window_size;
        unsigned int timeout_ms;

        if (argc > 7) {
            print_usage(argv[0]);
            return 1;
        }

        mode_text = (argc >= 3) ? argv[2] : "all";
        loss_rate = (argc >= 4) ? atof(argv[3]) : 0.20;
        window_size = (argc >= 5) ? (size_t)atoi(argv[4]) : DLINK_DEFAULT_WINDOW;
        timeout_ms = (argc >= 6) ? (unsigned int)atoi(argv[5]) : 250U;
        input_file_path = (argc >= 7) ? argv[6] : NULL;
        return run_datalink_demo(mode_text, loss_rate, window_size, timeout_ms, input_file_path);
    }

    if (strcmp(argv[1], "ping") == 0) {
        if (argc != 3 && argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        if (argc == 4 && strcmp(argv[3], "-t") != 0) {
            print_usage(argv[0]);
            return 1;
        }
        return run_ping(argv[2], argc == 4);
    }

    if (strcmp(argv[1], "capture") == 0) {
        int protocol_filter;

        protocol_filter = 0;
        if (argc == 4) {
            protocol_filter = parse_capture_protocol_filter(argv[3]);
            if (protocol_filter < 0) {
                print_usage(argv[0]);
                return 1;
            }
        }

        if (argc == 3 && strcmp(argv[2], "demo") == 0) {
            return run_capture_demo_all();
        }
        if (argc == 4 && strcmp(argv[2], "demo") == 0) {
            return run_capture_demo(protocol_filter);
        }
        if (argc != 3 && argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        return run_live_capture(argv[2], protocol_filter);
    }

    print_usage(argv[0]);
    return 1;
}
