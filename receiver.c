#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PAYLOAD_SIZE 160
#define RAW_FRAME_SIZE 164
#define WIRE_PAYLOAD_SIZE (RAW_FRAME_SIZE + RAW_FRAME_SIZE)
#define MAX_FRAMES 10000

typedef struct {
    int received;
    unsigned char payload[PAYLOAD_SIZE];
} FrameSlot;

static FrameSlot g_slots[MAX_FRAMES];
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_out_fd = -1;
static struct sockaddr_in g_player_addr;

static double g_t0 = 0.0;
static double g_delay_ms = 40.0;

void* playout_thread(void* arg) {
    int next_frame = 0;
    while (1) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        double now = ts.tv_sec + ts.tv_nsec / 1e9;

        // Playout target time for next_frame
        double target = g_t0 + (g_delay_ms / 1000.0) + (next_frame * 0.020);

        if (now < target) {
            double sleep_sec = target - now;
            usleep((useconds_t)(sleep_sec * 1e6));
        }

        pthread_mutex_lock(&g_mutex);
        if (g_slots[next_frame].received) {
            unsigned char out_buf[RAW_FRAME_SIZE];
            uint32_t seq_be = htonl(next_frame);
            memcpy(out_buf, &seq_be, 4);
            memcpy(out_buf + 4, g_slots[next_frame].payload, PAYLOAD_SIZE);

            sendto(g_out_fd, out_buf, RAW_FRAME_SIZE, 0,
                   (struct sockaddr *)&g_player_addr, sizeof(g_player_addr));
        }
        pthread_mutex_unlock(&g_mutex);

        next_frame++;
        if (next_frame >= MAX_FRAMES) break;
    }
    return NULL;
}

int main(void) {
    char *env_t0 = getenv("T0");
    char *env_delay = getenv("DELAY_MS");
    if (env_t0) g_t0 = atof(env_t0);
    if (env_delay) g_delay_ms = atof(env_delay);

    memset(g_slots, 0, sizeof(g_slots));

    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0) {
        perror("bind 47002");
        return 1;
    }

    g_out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    g_player_addr.sin_family = AF_INET;
    g_player_addr.sin_port = htons(47020);
    g_player_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    pthread_t pth;
    pthread_create(&pth, NULL, playout_thread, NULL);

    unsigned char buf[2048];
    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < WIRE_PAYLOAD_SIZE) continue;

        // Process Frame 1 (Primary)
        uint32_t seq1;
        memcpy(&seq1, buf, 4);
        seq1 = ntohl(seq1);

        // Process Frame 2 (Piggybacked redundancy)
        uint32_t seq2;
        memcpy(&seq2, buf + RAW_FRAME_SIZE, 4);
        seq2 = ntohl(seq2);

        pthread_mutex_lock(&g_mutex);
        if (seq1 < MAX_FRAMES && !g_slots[seq1].received) {
            memcpy(g_slots[seq1].payload, buf + 4, PAYLOAD_SIZE);
            g_slots[seq1].received = 1;
        }
        if (seq2 < MAX_FRAMES && !g_slots[seq2].received) {
            // Check if valid redundant packet (not dummy zero seq)
            if (seq2 > 0 || buf[RAW_FRAME_SIZE + 4] != 0) {
                memcpy(g_slots[seq2].payload, buf + RAW_FRAME_SIZE + 4, PAYLOAD_SIZE);
                g_slots[seq2].received = 1;
            }
        }
        pthread_mutex_unlock(&g_mutex);
    }
    return 0;
}
