#include "protocol_structs.h"

#include <stdio.h>
#include <string.h>

#include <WinSock2.h>
#include <WS2tcpip.h>

#include "utils.h"

#define PACKET_PREVIEW_BYTES 16U

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
    header->type = ICMP_TYPE_ECHO_REQUEST;
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
    if (icmp->type != ICMP_TYPE_ECHO_REPLY || icmp->code != 0) {
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

const char *ip_protocol_name(uint8_t protocol)
{
    switch (protocol) {
        case IP_PROTO_ICMP: return "ICMP";
        case IP_PROTO_TCP: return "TCP";
        case IP_PROTO_UDP: return "UDP";
        default: return "OTHER";
    }
}

void format_tcp_flags(uint8_t flags, char *output, size_t output_size)
{
    size_t used;

    if (output == NULL || output_size == 0) {
        return;
    }

    output[0] = '\0';
    used = 0;

    if ((flags & TCP_FLAG_FIN) != 0U && used + 4U < output_size) {
        strncpy(output + used, "FIN ", output_size - used - 1U);
        used += 4U;
    }
    if ((flags & TCP_FLAG_SYN) != 0U && used + 4U < output_size) {
        strncpy(output + used, "SYN ", output_size - used - 1U);
        used += 4U;
    }
    if ((flags & TCP_FLAG_RST) != 0U && used + 4U < output_size) {
        strncpy(output + used, "RST ", output_size - used - 1U);
        used += 4U;
    }
    if ((flags & TCP_FLAG_PSH) != 0U && used + 4U < output_size) {
        strncpy(output + used, "PSH ", output_size - used - 1U);
        used += 4U;
    }
    if ((flags & TCP_FLAG_ACK) != 0U && used + 4U < output_size) {
        strncpy(output + used, "ACK ", output_size - used - 1U);
        used += 4U;
    }
    if ((flags & TCP_FLAG_URG) != 0U && used + 4U < output_size) {
        strncpy(output + used, "URG ", output_size - used - 1U);
        used += 4U;
    }

    if (used == 0) {
        strncpy(output, "NONE", output_size - 1U);
        output[output_size - 1U] = '\0';
        return;
    }

    if (used > 0 && output[used - 1U] == ' ') {
        output[used - 1U] = '\0';
    }
}

static void print_payload_preview(const uint8_t *payload, size_t payload_length)
{
    size_t index;
    size_t preview_length;

    if (payload == NULL || payload_length == 0) {
        printf("Payload preview: <empty>\n");
        return;
    }

    preview_length = (payload_length < PACKET_PREVIEW_BYTES) ? payload_length : PACKET_PREVIEW_BYTES;

    printf("Payload preview (hex): ");
    for (index = 0; index < preview_length; ++index) {
        printf("%02X ", payload[index]);
    }
    if (payload_length > preview_length) {
        printf("...");
    }
    printf("\n");

    printf("Payload preview (ascii): ");
    for (index = 0; index < preview_length; ++index) {
        uint8_t ch;
        ch = payload[index];
        if (ch >= 32U && ch <= 126U) {
            putchar((int)ch);
        } else {
            putchar('.');
        }
    }
    if (payload_length > preview_length) {
        printf("...");
    }
    printf("\n");
}

void print_parsed_packet_summary(const parsed_packet_t *packet)
{
    char source_mac[32];
    char destination_mac[32];
    char source_ip[32];
    char destination_ip[32];

    if (packet == NULL || packet->ethernet == NULL || packet->ipv4 == NULL) {
        printf("packet summary unavailable\n");
        return;
    }

    format_mac_address(packet->ethernet->source, source_mac, sizeof(source_mac));
    format_mac_address(packet->ethernet->destination, destination_mac, sizeof(destination_mac));
    format_ipv4_address(packet->ipv4->source_ip, source_ip, sizeof(source_ip));
    format_ipv4_address(packet->ipv4->destination_ip, destination_ip, sizeof(destination_ip));

    printf("Ethernet: %s -> %s, type=0x%04X\n",
        source_mac,
        destination_mac,
        ntohs(packet->ethernet->ether_type));
    printf("IPv4: %s -> %s, ttl=%u, protocol=%s, payload=%lu bytes\n",
        source_ip,
        destination_ip,
        (unsigned int)packet->ipv4->ttl,
        ip_protocol_name(packet->ipv4->protocol),
        (unsigned long)packet->payload_length);

    if (packet->icmp != NULL) {
        printf("ICMP: type=%u code=%u id=%u seq=%u\n",
            (unsigned int)packet->icmp->type,
            (unsigned int)packet->icmp->code,
            (unsigned int)ntohs(packet->icmp->identifier),
            (unsigned int)ntohs(packet->icmp->sequence_number));
        print_payload_preview(packet->payload, packet->payload_length);
        return;
    }

    if (packet->tcp != NULL) {
        char tcp_flags[64];
        format_tcp_flags(packet->tcp->flags, tcp_flags, sizeof(tcp_flags));
        printf("TCP: %u -> %u, seq=%lu ack=%lu flags=%s\n",
            (unsigned int)ntohs(packet->tcp->source_port),
            (unsigned int)ntohs(packet->tcp->destination_port),
            (unsigned long)ntohl(packet->tcp->sequence_number),
            (unsigned long)ntohl(packet->tcp->acknowledgement_number),
            tcp_flags);
        print_payload_preview(packet->payload, packet->payload_length);
        return;
    }

    if (packet->udp != NULL) {
        printf("UDP: %u -> %u, length=%u\n",
            (unsigned int)ntohs(packet->udp->source_port),
            (unsigned int)ntohs(packet->udp->destination_port),
            (unsigned int)ntohs(packet->udp->length));
        print_payload_preview(packet->payload, packet->payload_length);
    }
}
