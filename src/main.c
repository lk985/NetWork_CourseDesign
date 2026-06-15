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
    printf("  %s ftp-server <port>\n", program_name);
    printf("  %s ftp-client <host> <port>\n", program_name);
    printf("  %s datalink-demo\n", program_name);
    printf("  %s ping <host> [-t]\n", program_name);
    printf("  %s capture <local-ip>\n", program_name);
}

static int run_datalink_demo(void)
{
    static const uint8_t sample1[] = "frame-one";
    static const uint8_t sample2[] = "frame-two";
    datalink_simulator_t *simulator;
    int status;

    simulator = datalink_simulator_create(DLINK_MODE_GBN, DLINK_DEFAULT_WINDOW, 250, 0.20);
    if (simulator == NULL) {
        log_message(LOG_LEVEL_ERROR, "failed to create datalink simulator");
        return 1;
    }

    datalink_simulator_queue_payload(simulator, sample1, sizeof(sample1) - 1U);
    datalink_simulator_queue_payload(simulator, sample2, sizeof(sample2) - 1U);

    status = datalink_simulator_run(simulator);
    datalink_simulator_print_stats(simulator);
    datalink_simulator_destroy(simulator);
    return status;
}

static int run_capture_demo(void)
{
    static const uint8_t demo_payload[] = "HELLO_TCP_DEMO";
    uint8_t packet_buffer[sizeof(ethernet_header_t) + sizeof(ipv4_header_t) + sizeof(tcp_header_t) + sizeof(demo_payload) - 1U];
    ethernet_header_t *ethernet;
    ipv4_header_t *ipv4;
    tcp_header_t *tcp;
    uint8_t *payload;
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
    ipv4->total_length = htons((uint16_t)(sizeof(ipv4_header_t) + sizeof(tcp_header_t) + sizeof(demo_payload) - 1U));
    ipv4->ttl = 64;
    ipv4->protocol = IP_PROTO_TCP;
    ipv4->source_ip = inet_addr("192.168.1.10");
    ipv4->destination_ip = inet_addr("192.168.1.20");

    tcp = (tcp_header_t *)(packet_buffer + sizeof(ethernet_header_t) + sizeof(ipv4_header_t));
    tcp->source_port = htons(12345);
    tcp->destination_port = htons(80);
    tcp->sequence_number = htonl(1001UL);
    tcp->acknowledgement_number = htonl(2002UL);
    tcp->data_offset_reserved = (uint8_t)(5U << 4);
    tcp->flags = (uint8_t)(TCP_FLAG_PSH | TCP_FLAG_ACK);
    tcp->window_size = htons(4096);

    payload = packet_buffer + sizeof(ethernet_header_t) + sizeof(ipv4_header_t) + sizeof(tcp_header_t);
    memcpy(payload, demo_payload, sizeof(demo_payload) - 1U);

    result = parse_ethernet_ipv4_packet(packet_buffer, sizeof(packet_buffer), &packet);
    if (result != PACKET_PARSE_OK) {
        log_message(LOG_LEVEL_ERROR, "capture demo parse failed, code=%d", (int)result);
        return 1;
    }

    printf("capture demo packet parsed successfully:\n");
    print_parsed_packet_summary(&packet);
    return 0;
}

static int run_live_capture(const char *local_ip)
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

    printf("capture listening on %s, waiting for one IPv4 packet...\n", local_ip);
    received_length = nw_recv_raw_bytes(capture_socket, raw_packet, sizeof(raw_packet), &remote_endpoint);
    if (received_length <= 0) {
        log_socket_error("receive raw packet", nw_last_error());
        nw_close_socket(capture_socket);
        nw_cleanup_winsock();
        return 1;
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
        log_message(LOG_LEVEL_ERROR, "captured packet parse failed, code=%d", (int)result);
        nw_close_socket(capture_socket);
        nw_cleanup_winsock();
        return 1;
    }

    printf("captured one packet from %s\n", remote_endpoint.host[0] != '\0' ? remote_endpoint.host : local_ip);
    print_parsed_packet_summary(&packet);

    nw_close_socket(capture_socket);
    nw_cleanup_winsock();
    return 0;
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
        if (argc != 3) {
            print_usage(argv[0]);
            return 1;
        }
        return ftp_server_run((unsigned short)atoi(argv[2]));
    }

    if (strcmp(argv[1], "ftp-client") == 0) {
        if (argc != 4) {
            print_usage(argv[0]);
            return 1;
        }
        return ftp_client_run(argv[2], (unsigned short)atoi(argv[3]));
    }

    if (strcmp(argv[1], "datalink-demo") == 0) {
        return run_datalink_demo();
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
        if (argc == 3 && strcmp(argv[2], "demo") == 0) {
            return run_capture_demo();
        }
        if (argc != 3) {
            print_usage(argv[0]);
            return 1;
        }
        return run_live_capture(argv[2]);
    }

    print_usage(argv[0]);
    return 1;
}
