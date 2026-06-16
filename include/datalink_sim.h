#ifndef DATALINK_SIM_H
#define DATALINK_SIM_H

#include <stddef.h>
#include <stdint.h>

#include "protocol_structs.h"

typedef enum datalink_mode {
    DLINK_MODE_STOP_AND_WAIT = 0,
    DLINK_MODE_GBN = 1
} datalink_mode_t;

typedef struct datalink_frame {
    datalink_frame_header_t header;
    uint8_t payload[DLINK_MAX_PAYLOAD_SIZE];
} datalink_frame_t;

typedef struct datalink_stats {
    unsigned int sent_frames;
    unsigned int resent_frames;
    unsigned int acked_frames;
    unsigned int delivered_frames;
    unsigned int dropped_frames;
    unsigned int dropped_acks;
    unsigned int timeout_events;
    unsigned int checksum_errors;
    unsigned int rounds;
} datalink_stats_t;

typedef struct datalink_simulator datalink_simulator_t;

datalink_simulator_t *datalink_simulator_create(
    datalink_mode_t mode,
    size_t window_size,
    unsigned int timeout_ms,
    double loss_rate
);
void datalink_simulator_destroy(datalink_simulator_t *simulator);

int datalink_simulator_queue_payload(
    datalink_simulator_t *simulator,
    const uint8_t *payload,
    size_t payload_length
);
int datalink_simulator_run(datalink_simulator_t *simulator);
void datalink_simulator_print_stats(const datalink_simulator_t *simulator);
const datalink_stats_t *datalink_simulator_get_stats(const datalink_simulator_t *simulator);

#endif
