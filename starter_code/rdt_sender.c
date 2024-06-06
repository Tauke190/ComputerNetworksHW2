#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include "packet.h"
#include "common.h"

#define STDIN_FD    0
#define RETRY  120 // millisecond
#define WINDOW_SIZE 1

int next_seqno = 0; // The next sequence number of the packet to be sent by the sender
int send_base = 0;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt[WINDOW_SIZE];
tcp_packet *recvpkt;
sigset_t sigmask;

void resend_packets(int sig) {
    if (sig == SIGALRM) {
        VLOG(INFO, "Timeout happened");
        for (int i = send_base; i < next_seqno; i++) {
            if (sndpkt[i % WINDOW_SIZE] != NULL) {
                if (sendto(sockfd, sndpkt[i % WINDOW_SIZE], TCP_HDR_SIZE + get_data_size(sndpkt[i % WINDOW_SIZE]), 0,
                    (const struct sockaddr *)&serveraddr, serverlen) < 0) {
                    error("sendto");
                }
                VLOG(INFO, "Resending packet %d", sndpkt[i % WINDOW_SIZE]->hdr.seqno);
            }
        }
    }
}

void start_timer() {
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void stop_timer() {
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

void init_timer(int delay, void (*sig_handler)(int)) {
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    timer.it_value.tv_sec = delay / 1000;
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

int main(int argc, char **argv) {
    int portno, len;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    init_timer(RETRY, resend_packets);
    next_seqno = 0;

    int eof = 0; // Flag to indicate end of file

    while (1) {
        // Send packets within the window
        while (next_seqno < send_base + WINDOW_SIZE && !eof) { // loop to send packets as long as there is window empty
            len = fread(buffer, 1, DATA_SIZE, fp);
            if (len <= 0) {
                eof = 1; // Mark end of file
                VLOG(INFO, "End Of File has been reached");
                sndpkt[next_seqno % WINDOW_SIZE] = make_packet(0);
                sndpkt[next_seqno % WINDOW_SIZE]->hdr.seqno = next_seqno;

                VLOG(DEBUG, "Sending EOF packet %d to %s",
                    next_seqno, inet_ntoa(serveraddr.sin_addr));

                if (sendto(sockfd, sndpkt[next_seqno % WINDOW_SIZE], TCP_HDR_SIZE, 0,
                    (const struct sockaddr *)&serveraddr, serverlen) < 0) {
                    error("sendto");
                }

                if (send_base == next_seqno) {
                    start_timer();     // Start the timer for the oldest unacked
                }

                next_seqno++;
                break;
            }

            sndpkt[next_seqno % WINDOW_SIZE] = make_packet(len);
            memcpy(sndpkt[next_seqno % WINDOW_SIZE]->data, buffer, len);
            sndpkt[next_seqno % WINDOW_SIZE]->hdr.seqno = next_seqno;

            VLOG(DEBUG, "Sending packet %d to %s",
                next_seqno, inet_ntoa(serveraddr.sin_addr));

            if (sendto(sockfd, sndpkt[next_seqno % WINDOW_SIZE], TCP_HDR_SIZE + get_data_size(sndpkt[next_seqno % WINDOW_SIZE]), 0,
                (const struct sockaddr *)&serveraddr, serverlen) < 0) {
                error("sendto");
            }

            if (send_base == next_seqno) {
                start_timer();
            }

            next_seqno += len;  // Increment by the length of the payload
        }

        // Wait for acknowledgments
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0) {
            error("recvfrom");
        }

        recvpkt = (tcp_packet *)buffer;
        printf("%d \n", get_data_size(recvpkt));
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        if (recvpkt->hdr.ackno >= send_base) {
            send_base = recvpkt->hdr.ackno + 1;
            if (send_base == next_seqno) {
                stop_timer();
            } else {
                start_timer();
            }
        }

        // Exit outer loop if EOF reached and all packets are acknowledged
        if (eof && send_base == next_seqno) {
            VLOG(INFO, "File transfer completed.");
            break;
        }
    }

    return 0;
}