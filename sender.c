#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PAYLOAD_SIZE 160
#define RAW_FRAME_SIZE 164
#define WIRE_PAYLOAD_SIZE (RAW_FRAME_SIZE + RAW_FRAME_SIZE)

int main(void) {
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0) {
        perror("bind 47010");
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay = {0};
    relay.sin_family = AF_INET;
    relay.sin_port = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    unsigned char prev_frame[RAW_FRAME_SIZE];
    int has_prev = 0;

    unsigned char in_buf[2048];
    unsigned char wire_buf[WIRE_PAYLOAD_SIZE];

    for (;;) {
        ssize_t n = recvfrom(in_fd, in_buf, sizeof(in_buf), 0, NULL, NULL);
        if (n != RAW_FRAME_SIZE) continue;

        // Wire Format: [Curr Frame (164 bytes)] + [Prev Frame (164 bytes if available, else 164 zeros)]
        memcpy(wire_buf, in_buf, RAW_FRAME_SIZE);
        if (has_prev) {
            memcpy(wire_buf + RAW_FRAME_SIZE, prev_frame, RAW_FRAME_SIZE);
        } else {
            memset(wire_buf + RAW_FRAME_SIZE, 0, RAW_FRAME_SIZE);
        }

        // Send current packet (Contains frame i and frame i-1)
        sendto(out_fd, wire_buf, WIRE_PAYLOAD_SIZE, 0, (struct sockaddr *)&relay, sizeof(relay));

        // Save current frame as previous for next iteration
        memcpy(prev_frame, in_buf, RAW_FRAME_SIZE);
        has_prev = 1;
    }
    return 0;
}
