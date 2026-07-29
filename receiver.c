#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define RECEIVER_PORT 47002
#define PLAYER_PORT 47020

#define PAYLOAD_SIZE 160
#define RAW_FRAME_SIZE 164
#define WIRE_PACKET_SIZE (2 * RAW_FRAME_SIZE)

#define MAX_FRAMES 10000
#define FRAME_INTERVAL_NS 20000000L
#define INVALID_SEQUENCE UINT32_MAX

typedef struct {
    int received;
    unsigned char payload[PAYLOAD_SIZE];
} FrameSlot;

static FrameSlot g_slots[MAX_FRAMES];

static pthread_mutex_t g_slots_mutex = PTHREAD_MUTEX_INITIALIZER;

static int g_player_fd = -1;
static struct sockaddr_in g_player_addr;

static struct timespec g_start_time;
static long g_delay_ns = 40000000L;

static int create_udp_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        perror("socket");
    }

    return fd;
}

static int parse_double(
    const char *text,
    double *result
) {
    if (text == NULL || result == NULL) {
        return -1;
    }

    char *end = NULL;
    errno = 0;

    double value = strtod(text, &end);

    if (
        errno != 0 ||
        end == text ||
        *end != '\0'
    ) {
        return -1;
    }

    *result = value;
    return 0;
}

static struct timespec seconds_to_timespec(double seconds) {
    struct timespec result;

    result.tv_sec = (time_t)seconds;

    double fractional = seconds - (double)result.tv_sec;
    result.tv_nsec = (long)(fractional * 1000000000.0);

    if (result.tv_nsec >= 1000000000L) {
        result.tv_sec++;
        result.tv_nsec -= 1000000000L;
    }

    if (result.tv_nsec < 0) {
        result.tv_sec--;
        result.tv_nsec += 1000000000L;
    }

    return result;
}

static struct timespec add_nanoseconds(
    struct timespec base,
    int64_t nanoseconds
) {
    base.tv_sec += nanoseconds / 1000000000LL;
    base.tv_nsec += nanoseconds % 1000000000LL;

    if (base.tv_nsec >= 1000000000L) {
        base.tv_sec++;
        base.tv_nsec -= 1000000000L;
    }

    if (base.tv_nsec < 0) {
        base.tv_sec--;
        base.tv_nsec += 1000000000L;
    }

    return base;
}

static int sleep_until_realtime(
    const struct timespec *deadline
) {
    int result;

    do {
        result = clock_nanosleep(
            CLOCK_REALTIME,
            TIMER_ABSTIME,
            deadline,
            NULL
        );
    } while (result == EINTR);

    return result;
}

static void store_frame(
    uint32_t sequence,
    const unsigned char *payload
) {
    if (sequence == INVALID_SEQUENCE) {
        return;
    }

    if (sequence >= MAX_FRAMES) {
        fprintf(
            stderr,
            "Ignoring out-of-range sequence number: %u\n",
            sequence
        );
        return;
    }

    pthread_mutex_lock(&g_slots_mutex);

    if (!g_slots[sequence].received) {
        memcpy(
            g_slots[sequence].payload,
            payload,
            PAYLOAD_SIZE
        );

        g_slots[sequence].received = 1;
    }

    pthread_mutex_unlock(&g_slots_mutex);
}

static void *playout_thread(void *argument) {
    (void)argument;

    for (
        uint32_t next_frame = 0;
        next_frame < MAX_FRAMES;
        next_frame++
    ) {
        int64_t frame_offset_ns =
            (int64_t)next_frame * FRAME_INTERVAL_NS;

        struct timespec deadline = add_nanoseconds(
            g_start_time,
            (int64_t)g_delay_ns + frame_offset_ns
        );

        int sleep_result = sleep_until_realtime(&deadline);

        if (sleep_result != 0) {
            fprintf(
                stderr,
                "clock_nanosleep failed: %s\n",
                strerror(sleep_result)
            );
            continue;
        }

        unsigned char output_frame[RAW_FRAME_SIZE];
        int should_send = 0;

        pthread_mutex_lock(&g_slots_mutex);

        if (g_slots[next_frame].received) {
            uint32_t sequence_be = htonl(next_frame);

            memcpy(
                output_frame,
                &sequence_be,
                sizeof(sequence_be)
            );

            memcpy(
                output_frame + sizeof(sequence_be),
                g_slots[next_frame].payload,
                PAYLOAD_SIZE
            );

            should_send = 1;
        }

        pthread_mutex_unlock(&g_slots_mutex);

        if (!should_send) {
            fprintf(
                stderr,
                "Playout miss for frame %u\n",
                next_frame
            );
            continue;
        }

        ssize_t sent = sendto(
            g_player_fd,
            output_frame,
            sizeof(output_frame),
            0,
            (struct sockaddr *)&g_player_addr,
            sizeof(g_player_addr)
        );

        if (sent < 0) {
            perror("sendto player");
            continue;
        }

        if (sent != RAW_FRAME_SIZE) {
            fprintf(
                stderr,
                "Partial player send: sent %zd of %d bytes\n",
                sent,
                RAW_FRAME_SIZE
            );
        }
    }

    return NULL;
}

int main(void) {
    const char *t0_text = getenv("T0");
    const char *delay_text = getenv("DELAY_MS");

    if (t0_text == NULL) {
        fprintf(
            stderr,
            "Error: T0 environment variable is required.\n"
            "Example: T0=1785346200.000 DELAY_MS=60 ./receiver\n"
        );

        return 1;
    }

    double start_seconds = 0.0;

    if (parse_double(t0_text, &start_seconds) != 0) {
        fprintf(
            stderr,
            "Error: invalid T0 value: %s\n",
            t0_text
        );

        return 1;
    }

    g_start_time = seconds_to_timespec(start_seconds);

    if (delay_text != NULL) {
        double delay_ms = 0.0;

        if (
            parse_double(delay_text, &delay_ms) != 0 ||
            delay_ms < 0.0
        ) {
            fprintf(
                stderr,
                "Error: invalid DELAY_MS value: %s\n",
                delay_text
            );

            return 1;
        }

        g_delay_ns = (long)(delay_ms * 1000000.0);
    }

    memset(g_slots, 0, sizeof(g_slots));

    int receiver_fd = create_udp_socket();

    if (receiver_fd < 0) {
        return 1;
    }

    int reuse = 1;
    if (setsockopt(
            receiver_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)
        ) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(receiver_fd);
        return 1;
    }

    struct sockaddr_in receiver_addr;
    memset(&receiver_addr, 0, sizeof(receiver_addr));

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(RECEIVER_PORT);
    receiver_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(
            receiver_fd,
            (struct sockaddr *)&receiver_addr,
            sizeof(receiver_addr)
        ) < 0) {
        perror("bind receiver port");
        close(receiver_fd);
        return 1;
    }

    g_player_fd = create_udp_socket();

    if (g_player_fd < 0) {
        close(receiver_fd);
        return 1;
    }

    memset(&g_player_addr, 0, sizeof(g_player_addr));

    g_player_addr.sin_family = AF_INET;
    g_player_addr.sin_port = htons(PLAYER_PORT);
    g_player_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    pthread_t playout_thread_id;

    int thread_result = pthread_create(
        &playout_thread_id,
        NULL,
        playout_thread,
        NULL
    );

    if (thread_result != 0) {
        fprintf(
            stderr,
            "pthread_create failed: %s\n",
            strerror(thread_result)
        );

        close(g_player_fd);
        close(receiver_fd);
        return 1;
    }

    fprintf(
        stderr,
        "Receiver listening on 127.0.0.1:%d\n",
        RECEIVER_PORT
    );

    unsigned char wire_packet[WIRE_PACKET_SIZE];

    for (;;) {
        ssize_t received = recvfrom(
            receiver_fd,
            wire_packet,
            sizeof(wire_packet),
            0,
            NULL,
            NULL
        );

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("recvfrom receiver");
            break;
        }

        if (received != WIRE_PACKET_SIZE) {
            fprintf(
                stderr,
                "Ignoring invalid wire packet size: %zd bytes\n",
                received
            );
            continue;
        }

        uint32_t current_sequence_be;
        uint32_t previous_sequence_be;

        memcpy(
            &current_sequence_be,
            wire_packet,
            sizeof(current_sequence_be)
        );

        memcpy(
            &previous_sequence_be,
            wire_packet + RAW_FRAME_SIZE,
            sizeof(previous_sequence_be)
        );

        uint32_t current_sequence =
            ntohl(current_sequence_be);

        uint32_t previous_sequence =
            ntohl(previous_sequence_be);

        store_frame(
            current_sequence,
            wire_packet + sizeof(uint32_t)
        );

        store_frame(
            previous_sequence,
            wire_packet +
                RAW_FRAME_SIZE +
                sizeof(uint32_t)
        );
    }

    pthread_cancel(playout_thread_id);
    pthread_join(playout_thread_id, NULL);

    close(g_player_fd);
    close(receiver_fd);

    return 1;
}
