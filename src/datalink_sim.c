#include "datalink_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utils.h"

#define DLINK_QUEUE_CAPACITY 32
#define DLINK_MAX_TIMEOUT_EVENTS 256U

struct datalink_simulator {
    datalink_mode_t mode;
    size_t window_size;
    unsigned int timeout_ms;
    double loss_rate;
    datalink_stats_t stats;
    datalink_frame_t queue[DLINK_QUEUE_CAPACITY];
    size_t queued_count;
    uint8_t next_sequence;
    unsigned int send_attempts[DLINK_QUEUE_CAPACITY];
};

static double random_unit_value(void)
{
    return (double)rand() / (double)RAND_MAX;
}

static const char *datalink_mode_name(datalink_mode_t mode)
{
    return (mode == DLINK_MODE_GBN) ? "GBN" : "STOP_WAIT";
}

static void prepare_frame(
    datalink_simulator_t *simulator,
    datalink_frame_t *frame,
    const uint8_t *payload,
    size_t payload_length
)
{
    memset(frame, 0, sizeof(*frame));
    frame->header.frame_type = (uint8_t)simulator->mode;
    frame->header.sequence_number = simulator->next_sequence++;
    frame->header.acknowledgement_number = 0;
    frame->header.payload_length = (uint8_t)payload_length;
    memcpy(frame->payload, payload, payload_length);
    frame->header.checksum = compute_crc32(frame->payload, payload_length);
}

datalink_simulator_t *datalink_simulator_create(
    datalink_mode_t mode,
    size_t window_size,
    unsigned int timeout_ms,
    double loss_rate
)
{
    datalink_simulator_t *simulator;
    static int random_seeded = 0;

    simulator = (datalink_simulator_t *)calloc(1, sizeof(*simulator));
    if (simulator == NULL) {
        return NULL;
    }

    simulator->mode = mode;
    simulator->window_size = (mode == DLINK_MODE_STOP_AND_WAIT) ? 1U : ((window_size == 0) ? 1U : window_size);
    simulator->timeout_ms = timeout_ms;
    simulator->loss_rate = loss_rate;
    if (!random_seeded) {
        srand((unsigned int)time(NULL));
        random_seeded = 1;
    }
    return simulator;
}

void datalink_simulator_destroy(datalink_simulator_t *simulator)
{
    free(simulator);
}

int datalink_simulator_queue_payload(
    datalink_simulator_t *simulator,
    const uint8_t *payload,
    size_t payload_length
)
{
    if (simulator == NULL || payload == NULL || payload_length == 0 || payload_length > DLINK_MAX_PAYLOAD_SIZE) {
        return -1;
    }

    if (simulator->queued_count >= DLINK_QUEUE_CAPACITY) {
        return -1;
    }

    prepare_frame(
        simulator,
        &simulator->queue[simulator->queued_count],
        payload,
        payload_length
    );
    simulator->queued_count++;
    return 0;
}

static int simulate_frame_send(datalink_simulator_t *simulator, size_t frame_index)
{
    datalink_frame_t *frame;
    int retransmission;

    if (simulator == NULL || frame_index >= simulator->queued_count) {
        return -1;
    }

    frame = &simulator->queue[frame_index];
    retransmission = (simulator->send_attempts[frame_index] > 0U);
    simulator->send_attempts[frame_index]++;
    simulator->stats.sent_frames++;
    if (retransmission) {
        simulator->stats.resent_frames++;
    }

    log_message(
        LOG_LEVEL_INFO,
        "%s frame seq=%u len=%u mode=%s",
        retransmission ? "resend" : "send",
        (unsigned int)frame->header.sequence_number,
        (unsigned int)frame->header.payload_length,
        datalink_mode_name(simulator->mode)
    );
    if (!retransmission) {
        print_datalink_frame_summary_fields(&frame->header, frame->payload);
    }

    if (random_unit_value() < simulator->loss_rate) {
        simulator->stats.dropped_frames++;
        log_message(
            LOG_LEVEL_WARN,
            "frame seq=%u lost on channel",
            (unsigned int)frame->header.sequence_number
        );
        return 0;
    }

    if (compute_crc32(frame->payload, frame->header.payload_length) != frame->header.checksum) {
        simulator->stats.checksum_errors++;
        log_message(
            LOG_LEVEL_ERROR,
            "frame seq=%u crc mismatch",
            (unsigned int)frame->header.sequence_number
        );
        return -1;
    }

    return 1;
}

static int simulate_ack(datalink_simulator_t *simulator, const datalink_frame_t *frame)
{
    if (simulator == NULL || frame == NULL) {
        return 0;
    }

    if (random_unit_value() < simulator->loss_rate) {
        simulator->stats.dropped_acks++;
        log_message(
            LOG_LEVEL_WARN,
            "ack for seq=%u lost",
            (unsigned int)frame->header.sequence_number
        );
        return 0;
    }

    simulator->stats.acked_frames++;
    log_message(
        LOG_LEVEL_INFO,
        "ack frame seq=%u payload=\"%.*s\"",
        (unsigned int)frame->header.sequence_number,
        (int)frame->header.payload_length,
        (const char *)frame->payload
    );
    return 1;
}

static int run_stop_and_wait(datalink_simulator_t *simulator)
{
    size_t frame_index;

    for (frame_index = 0; frame_index < simulator->queued_count; ++frame_index) {
        for (;;) {
            int send_status;

            simulator->stats.rounds++;
            send_status = simulate_frame_send(simulator, frame_index);
            if (send_status < 0) {
                return -1;
            }

            if (send_status == 0 || !simulate_ack(simulator, &simulator->queue[frame_index])) {
                simulator->stats.timeout_events++;
                if (simulator->stats.timeout_events > DLINK_MAX_TIMEOUT_EVENTS) {
                    log_message(LOG_LEVEL_ERROR, "stop-and-wait simulation aborted: too many timeout events");
                    return -1;
                }
                log_message(
                    LOG_LEVEL_WARN,
                    "timeout waiting for seq=%u, retry after %u ms",
                    (unsigned int)simulator->queue[frame_index].header.sequence_number,
                    simulator->timeout_ms
                );
                continue;
            }

            simulator->stats.delivered_frames++;
            break;
        }
    }

    return 0;
}

static int run_go_back_n(datalink_simulator_t *simulator)
{
    size_t base;

    base = 0;
    while (base < simulator->queued_count) {
        size_t window_end;
        size_t committed_up_to;
        size_t failure_index;
        size_t frame_index;

        window_end = base + simulator->window_size;
        if (window_end > simulator->queued_count) {
            window_end = simulator->queued_count;
        }

        committed_up_to = base;
        failure_index = simulator->queued_count;
        simulator->stats.rounds++;
        log_message(
            LOG_LEVEL_INFO,
            "window open base_seq=%u size=%lu range=[%lu,%lu]",
            (unsigned int)simulator->queue[base].header.sequence_number,
            (unsigned long)simulator->window_size,
            (unsigned long)base,
            (unsigned long)(window_end - 1U)
        );

        for (frame_index = base; frame_index < window_end; ++frame_index) {
            int send_status;

            send_status = simulate_frame_send(simulator, frame_index);
            if (send_status < 0) {
                return -1;
            }

            if (send_status == 0) {
                if (failure_index == simulator->queued_count) {
                    failure_index = frame_index;
                }
                continue;
            }

            if (failure_index != simulator->queued_count) {
                log_message(
                    LOG_LEVEL_WARN,
                    "receiver discarded out-of-order frame seq=%u, waiting for seq=%u",
                    (unsigned int)simulator->queue[frame_index].header.sequence_number,
                    (unsigned int)simulator->queue[failure_index].header.sequence_number
                );
                continue;
            }

            if (!simulate_ack(simulator, &simulator->queue[frame_index])) {
                failure_index = frame_index;
                continue;
            }

            simulator->stats.delivered_frames++;
            committed_up_to = frame_index + 1U;
        }

        if (failure_index == simulator->queued_count) {
            base = window_end;
            continue;
        }

        simulator->stats.timeout_events++;
        if (simulator->stats.timeout_events > DLINK_MAX_TIMEOUT_EVENTS) {
            log_message(LOG_LEVEL_ERROR, "gbn simulation aborted: too many timeout events");
            return -1;
        }
        log_message(
            LOG_LEVEL_WARN,
            "timeout at seq=%u, go-back-n resend from seq=%u after %u ms",
            (unsigned int)simulator->queue[failure_index].header.sequence_number,
            (unsigned int)simulator->queue[committed_up_to].header.sequence_number,
            simulator->timeout_ms
        );
        base = committed_up_to;
    }

    return 0;
}

int datalink_simulator_run(datalink_simulator_t *simulator)
{
    if (simulator == NULL) {
        return -1;
    }

    if (simulator->mode == DLINK_MODE_GBN) {
        return run_go_back_n(simulator);
    }

    return run_stop_and_wait(simulator);
}

void datalink_simulator_print_stats(const datalink_simulator_t *simulator)
{
    const datalink_stats_t *stats;

    if (simulator == NULL) {
        return;
    }

    stats = &simulator->stats;
    printf(
        "datalink stats (%s): window=%lu timeout=%u loss=%.2f sent=%u resent=%u acked=%u delivered=%u frame_drop=%u ack_drop=%u timeout_event=%u crc_error=%u rounds=%u\n",
        datalink_mode_name(simulator->mode),
        (unsigned long)simulator->window_size,
        simulator->timeout_ms,
        simulator->loss_rate,
        stats->sent_frames,
        stats->resent_frames,
        stats->acked_frames,
        stats->delivered_frames,
        stats->dropped_frames,
        stats->dropped_acks,
        stats->timeout_events,
        stats->checksum_errors,
        stats->rounds
    );
}

const datalink_stats_t *datalink_simulator_get_stats(const datalink_simulator_t *simulator)
{
    return (simulator == NULL) ? NULL : &simulator->stats;
}
