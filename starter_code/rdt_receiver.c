#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"

#define WINDOW_SIZE 10

tcp_packet *recvpkt;
tcp_packet *sndpkt;
int anticipated_window[WINDOW_SIZE];
tcp_packet *packet_window[WINDOW_SIZE];

int anticipated_sequence = 0;

void fix_buffer_window() {
    if (anticipated_window[0] != 0) {
        anticipated_window[0] = 0;
    }

    for (int i = 1; i < WINDOW_SIZE; i++) {
        anticipated_window[i - 1] = anticipated_window[i];
    }

    anticipated_window[WINDOW_SIZE - 1] = 0;
}

void fill_anticipated_window(int accepted_num, int window_counter) {
    if (window_counter < WINDOW_SIZE) {
        anticipated_window[window_counter] = accepted_num;
    } else {
        fix_buffer_window();
        anticipated_window[WINDOW_SIZE - 1] = accepted_num;
    }
}

int is_num(int seqno) {
    for (int i = 0; i < WINDOW_SIZE; i++) {
        if (anticipated_window[i] == seqno) {
            return 1;
        }
    }
    return 0;
}

void fix_packet_window() {
    if (packet_window[0] != NULL) {
        free(packet_window[0]);
    }

    for (int i = 1; i < WINDOW_SIZE; i++) {
        packet_window[i - 1] = packet_window[i];
    }

    packet_window[WINDOW_SIZE - 1] = NULL;
}

void fill_received_packet_window(tcp_packet *recvpkt, int window_counter) {
    if (window_counter < WINDOW_SIZE) {
        packet_window[window_counter] = recvpkt;
    } else {
        fix_packet_window();
        packet_window[WINDOW_SIZE - 1] = recvpkt;
    }
}

int get_index(tcp_packet *recvpkt) {
    for (int i = 0; i < WINDOW_SIZE; i++) {
        if (recvpkt->hdr.seqno == packet_window[i]->hdr.seqno) {
            return i;
        }
    }
    return -1;
}

int main(int argc, char **argv) {
    int sockfd;
    int portno;
    int clientlen;
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    int optval;
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);

    int window_counter = 0;
    while (1) {
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *)buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        if (!is_num(recvpkt->hdr.seqno)) {
            if (recvpkt->hdr.seqno == anticipated_sequence) {
                if (recvpkt->hdr.data_size == 0) {
                    fclose(fp);
                    sndpkt = make_packet(0);
                    sndpkt->hdr.data_size = -1000;
                    if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
                        error("ERROR in sendto");
                    }
                    break;
                }

                fill_anticipated_window(anticipated_sequence, window_counter);
                fill_received_packet_window(recvpkt, window_counter);

                gettimeofday(&tp, NULL);
                VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

                fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
                fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
                anticipated_sequence += recvpkt->hdr.data_size;

                sndpkt = make_packet(0);
                sndpkt->hdr.ackno = anticipated_sequence;
                sndpkt->hdr.ctr_flags = ACK;
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
                    error("ERROR in sendto");
                }
                window_counter++;
            } else {
                sndpkt = make_packet(0);
                sndpkt->hdr.ackno = anticipated_sequence;
                sndpkt->hdr.ctr_flags = ACK;
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
                    error("ERROR in sendto");
                }
            }
        } else {
            int index = get_index(recvpkt);
            if (index >= 0) {
                sndpkt = make_packet(0);
                sndpkt->hdr.ackno = anticipated_window[index] + packet_window[index]->hdr.data_size;
                sndpkt->hdr.ctr_flags = ACK;
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
                    error("ERROR in sendto");
                }
            }
        }
    }

    return 0;
}
