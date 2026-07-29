#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define INPUT_PORT 47010
#define RELAY_PORT 47001

#define PAYLOAD_SIZE 160
#define RAW_FRAME_SIZE 164
#define WIRE_PACKET_SIZE (2 * RAW_FRAME_SIZE)

/*
 * Used in the redundant-frame sequence-number field when no previous
 * frame exists. Valid frames are expected to use normal sequence numbers.
 */
#define INVALID_SEQUENCE UINT32_MAX

static int create_udp_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        perror("socket");
    }

    return fd;
}

int main(void) {
    int in_fd = create_udp_socket();

    if (in_fd < 0) {
        return 1;
    }

    int reuse = 1;
    if (setsockopt(
            in_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)
        ) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(in_fd);
        return 1;
    }

    struct sockaddr_in input_addr;
    memset(&input_addr, 0, sizeof(input_addr));

    input_addr.sin_family = AF_INET;
    input_addr.sin_port = htons(INPUT_PORT);
    input_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(
            in_fd,
            (struct sockaddr *)&input_addr,
            sizeof(input_addr)
        ) < 0) {
        perror("bind sender input port");
        close(in_fd);
        return 1;
    }

    int out_fd = create_udp_socket();

    if (out_fd < 0) {
        close(in_fd);
        return 1;
    }

    struct sockaddr_in relay_addr;
    memset(&relay_addr, 0, sizeof(relay_addr));

    relay_addr.sin_family = AF_INET;
    relay_addr.sin_port = htons(RELAY_PORT);
    relay_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    unsigned char previous_frame[RAW_FRAME_SIZE];
    memset(previous_frame, 0, sizeof(previous_frame));

    /*
     * Mark the initial redundant frame as invalid.
     * The value is stored in network byte order.
     */
    uint32_t invalid_sequence_be = htonl(INVALID_SEQUENCE);
    memcpy(previous_frame, &invalid_sequence_be, sizeof(invalid_sequence_be));

    unsigned char input_buffer[RAW_FRAME_SIZE];
    unsigned char wire_packet[WIRE_PACKET_SIZE];

    fprintf(
        stderr,
        "Sender listening on 127.0.0.1:%d\n",
        INPUT_PORT
    );

    for (;;) {
        ssize_t received = recvfrom(
            in_fd,
            input_buffer,
            sizeof(input_buffer),
            0,
            NULL,
            NULL
        );

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("recvfrom sender input");
            break;
        }

        if (received != RAW_FRAME_SIZE) {
            fprintf(
                stderr,
                "Ignoring invalid raw frame size: %zd bytes\n",
                received
            );
            continue;
        }

        /*
         * Wire format:
         *
         * bytes 0-163   : current raw frame
         * bytes 164-327 : previous raw frame
         */
        memcpy(
            wire_packet,
            input_buffer,
            RAW_FRAME_SIZE
        );

        memcpy(
            wire_packet + RAW_FRAME_SIZE,
            previous_frame,
            RAW_FRAME_SIZE
        );

        ssize_t sent = sendto(
            out_fd,
            wire_packet,
            sizeof(wire_packet),
            0,
            (struct sockaddr *)&relay_addr,
            sizeof(relay_addr)
        );

        if (sent < 0) {
            perror("sendto relay");
            continue;
        }

        if (sent != WIRE_PACKET_SIZE) {
            fprintf(
                stderr,
                "Partial UDP send: sent %zd of %d bytes\n",
                sent,
                WIRE_PACKET_SIZE
            );
            continue;
        }

        memcpy(
            previous_frame,
            input_buffer,
            RAW_FRAME_SIZE
        );
    }

    close(out_fd);
    close(in_fd);

    return 1;
}
