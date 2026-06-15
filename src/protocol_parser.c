#include "protocol_structs.h"

#include <stdio.h>
#include <string.h>

#include <WinSock2.h>
#include <WS2tcpip.h>

#include "utils.h"

int ipv4_get_version(const ipv4_header_t *header)
{
    return (header == NULL) ? -1 : ((header->version_ihl >> 4) & 0x0F);
}

int ipv4_get_header_length(const ipv4_header_t *header)
{
    return (header == NULL) ? -1 : ((header->version_ihl & 0x0F) * 4);
}

int tcp_get_header_length(const tcp_header_t *header)
{
    return (header == NULL) ? -1 : (((header->data_offset_reserved >> 4) & 0x0F) * 4);
}

packet_parse_result_t parse_ethernet_ipv4_packet(
    const uint8_t *buffer,
    size_t buffer_length,
    parsed_packet_t *packet
)
{
    const ipv4_header_t *ipv4;
    size_t ip_header_length;
    size_t ip_total_length;
    const uint8_t *transport_layer;

    if (buffer == NULL || packet == NULL || buffer_length < sizeof(ethernet_header_t)) {
        return PACKET_PARSE_BUFFER_TOO_SMALL;
    }

    memset(packet, 0, sizeof(*packet));
    packet->ethernet = (const ethernet_header_t *)buffer;

    if (ntohs(packet->ethernet->ether_type) != ETHER_TYPE_IPV4) {
        return PACKET_PARSE_UNSUPPORTED;
    }

    if (buffer_length < sizeof(ethernet_header_t) + sizeof(ipv4_header_t)) {
        return PACKET_PARSE_BUFFER_TOO_SMALL;
    }

    ipv4 = (const ipv4_header_t *)(buffer + sizeof(ethernet_header_t));
    if (ipv4_get_version(ipv4) != 4) {
        return PACKET_PARSE_INVALID_FIELD;
    }

    ip_header_length = (size_t)ipv4_get_header_length(ipv4);
    ip_total_length = (size_t)ntohs(ipv4->total_length);
    if (ip_header_length < sizeof(ipv4_header_t) ||
        buffer_length < sizeof(ethernet_header_t) + ip_header_length ||
        ip_total_length < ip_header_length) {
        return PACKET_PARSE_BUFFER_TOO_SMALL;
    }

    packet->ipv4 = ipv4;
    transport_layer = buffer + sizeof(ethernet_header_t) + ip_header_length;
    packet->payload = transport_layer;
    packet->payload_length = ip_total_length - ip_header_length;

    if (ipv4->protocol == IP_PROTO_ICMP) {
        if (packet->payload_length < sizeof(icmp_header_t)) {
            return PACKET_PARSE_BUFFER_TOO_SMALL;
        }
        packet->icmp = (const icmp_header_t *)transport_layer;
        packet->payload = transport_layer + sizeof(icmp_header_t);
        packet->payload_length -= sizeof(icmp_header_t);
        return PACKET_PARSE_OK;
    }

    if (ipv4->protocol == IP_PROTO_TCP) {
        size_t tcp_header_length;
        if (packet->payload_length < sizeof(tcp_header_t)) {
            return PACKET_PARSE_BUFFER_TOO_SMALL;
        }
        packet->tcp = (const tcp_header_t *)transport_layer;
        tcp_header_length = (size_t)tcp_get_header_length(packet->tcp);
        if (tcp_header_length < sizeof(tcp_header_t) || packet->payload_length < tcp_header_length) {
            return PACKET_PARSE_INVALID_FIELD;
        }
        packet->payload = transport_layer + tcp_header_length;
        packet->payload_length -= tcp_header_length;
        return PACKET_PARSE_OK;
    }

    if (ipv4->protocol == IP_PROTO_UDP) {
        if (packet->payload_length < sizeof(udp_header_t)) {
            return PACKET_PARSE_BUFFER_TOO_SMALL;
        }
        packet->udp = (const udp_header_t *)transport_layer;
        packet->payload = transport_layer + sizeof(udp_header_t);
        packet->payload_length -= sizeof(udp_header_t);
        return PACKET_PARSE_OK;
    }

    return PACKET_PARSE_OK;
}

size_t build_icmp_echo_packet(
    uint8_t *buffer,
    size_t buffer_size,
    uint16_t identifier,
    uint16_t sequence_number,
    const void *payload,
    size_t payload_length
)
{
    icmp_header_t *header;
    size_t total_length;

    total_length = sizeof(icmp_header_t) + payload_length;
    if (buffer == NULL || buffer_size < total_length) {
        return 0;
    }

    memset(buffer, 0, total_length);
    header = (icmp_header_t *)buffer;
    header->type = ICMP_ECHO_REQUEST;
    header->code = 0;
    header->identifier = htons(identifier);
    header->sequence_number = htons(sequence_number);

    if (payload != NULL && payload_length > 0) {
        memcpy(buffer + sizeof(icmp_header_t), payload, payload_length);
    }

    header->checksum = 0;
    header->checksum = compute_internet_checksum(buffer, total_length);
    return total_length;
}

int parse_icmp_echo_reply(
    const uint8_t *buffer,
    size_t buffer_length,
    uint16_t expected_identifier,
    uint16_t *sequence_number
)
{
    const ipv4_header_t *ipv4;
    const icmp_header_t *icmp;
    size_t ip_header_length;

    if (buffer == NULL || buffer_length < sizeof(ipv4_header_t) + sizeof(icmp_header_t)) {
        return 0;
    }

    ipv4 = (const ipv4_header_t *)buffer;
    if (ipv4_get_version(ipv4) != 4 || ipv4->protocol != IP_PROTO_ICMP) {
        return 0;
    }

    ip_header_length = (size_t)ipv4_get_header_length(ipv4);
    if (buffer_length < ip_header_length + sizeof(icmp_header_t)) {
        return 0;
    }

    icmp = (const icmp_header_t *)(buffer + ip_header_length);
    if (icmp->type != ICMP_ECHO_REPLY || icmp->code != 0) {
        return 0;
    }

    if (ntohs(icmp->identifier) != expected_identifier) {
        return 0;
    }

    if (sequence_number != NULL) {
        *sequence_number = ntohs(icmp->sequence_number);
    }

    return 1;
}

void format_mac_address(const uint8_t address[ETH_ADDR_LEN], char *output, size_t output_size)
{
    if (output == NULL || output_size == 0 || address == NULL) {
        return;
    }

    (void)snprintf(
        output,
        output_size,
        "%02X-%02X-%02X-%02X-%02X-%02X",
        address[0],
        address[1],
        address[2],
        address[3],
        address[4],
        address[5]
    );
}

void format_ipv4_address(uint32_t network_order_ip, char *output, size_t output_size)
{
    struct in_addr address;
    const char *text;

    if (output == NULL || output_size == 0) {
        return;
    }

    address.s_addr = network_order_ip;
    text = inet_ntoa(address);
    if (text == NULL) {
        output[0] = '\0';
        return;
    }

    strncpy(output, text, output_size - 1U);
    output[output_size - 1U] = '\0';
}
