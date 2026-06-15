#ifndef PROTOCOL_STRUCTS_H
#define PROTOCOL_STRUCTS_H

#include <stddef.h>
#include <stdint.h>

#define ETH_ADDR_LEN 6
#define IPV4_ADDR_LEN 4

#define ETHER_TYPE_IPV4 0x0800u
#define ETHER_TYPE_ARP  0x0806u

#define IP_PROTO_ICMP 1u
#define IP_PROTO_TCP  6u
#define IP_PROTO_UDP  17u

#define ICMP_ECHO_REPLY   0u
#define ICMP_ECHO_REQUEST 8u
#define ICMP_DEFAULT_DATA_SIZE 32u

#define FTP_MAX_COMMAND_LEN 512
#define FTP_MAX_PATH_LEN    260
#define FTP_DATA_CHUNK_SIZE 1024

#define DLINK_MAX_PAYLOAD_SIZE 1024
#define DLINK_DEFAULT_WINDOW   4

#pragma pack(push, 1)

typedef struct ethernet_header {
    uint8_t destination[ETH_ADDR_LEN];
    uint8_t source[ETH_ADDR_LEN];
    uint16_t ether_type;
} ethernet_header_t;

typedef struct ipv4_header {
    uint8_t version_ihl;
    uint8_t type_of_service;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t source_ip;
    uint32_t destination_ip;
} ipv4_header_t;

typedef struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence_number;
} icmp_header_t;

typedef struct udp_header {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

typedef struct tcp_header {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence_number;
    uint32_t acknowledgement_number;
    uint8_t data_offset_reserved;
    uint8_t flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
} tcp_header_t;

typedef struct ftp_data_header {
    uint32_t sequence_number;
    uint32_t payload_length;
    uint8_t is_last_chunk;
    uint8_t reserved[3];
} ftp_data_header_t;

typedef struct datalink_frame_header {
    uint8_t frame_type;
    uint8_t sequence_number;
    uint8_t acknowledgement_number;
    uint8_t payload_length;
    uint32_t checksum;
} datalink_frame_header_t;

#pragma pack(pop)

typedef enum packet_parse_result {
    PACKET_PARSE_OK = 0,
    PACKET_PARSE_BUFFER_TOO_SMALL = -1,
    PACKET_PARSE_UNSUPPORTED = -2,
    PACKET_PARSE_INVALID_FIELD = -3
} packet_parse_result_t;

typedef struct parsed_packet {
    const ethernet_header_t *ethernet;
    const ipv4_header_t *ipv4;
    const icmp_header_t *icmp;
    const tcp_header_t *tcp;
    const udp_header_t *udp;
    const uint8_t *payload;
    size_t payload_length;
} parsed_packet_t;

int ipv4_get_version(const ipv4_header_t *header);
int ipv4_get_header_length(const ipv4_header_t *header);
int tcp_get_header_length(const tcp_header_t *header);

packet_parse_result_t parse_ethernet_ipv4_packet(
    const uint8_t *buffer,
    size_t buffer_length,
    parsed_packet_t *packet
);

size_t build_icmp_echo_packet(
    uint8_t *buffer,
    size_t buffer_size,
    uint16_t identifier,
    uint16_t sequence_number,
    const void *payload,
    size_t payload_length
);

int parse_icmp_echo_reply(
    const uint8_t *buffer,
    size_t buffer_length,
    uint16_t expected_identifier,
    uint16_t *sequence_number
);

void format_mac_address(const uint8_t address[ETH_ADDR_LEN], char *output, size_t output_size);
void format_ipv4_address(uint32_t network_order_ip, char *output, size_t output_size);

#endif
