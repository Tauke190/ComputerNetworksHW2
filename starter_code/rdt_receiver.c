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

#define window_size 10

tcp_packet *recvpkt;
tcp_packet *sndpkt;
int anticipated_window[window_size];

int anticipated_sequence = 0;

void fix_buffer_window() {
    if (anticipated_window[0] != 0) {
        anticipated_window[0] = 0;
    }

    for (int i = 1; i < window_size; i++) {
        anticipated_window[i - 1] = anticipated_window[i];
    }

    anticipated_window[window_size - 1] = 0;
}

void fill_anticipated_window(int accepted_num, int window_counter) {
    if (window_counter < window_size) {
        anticipated_window[window_counter] = accepted_num;
    } else {
        fix_buffer_window();
        anticipated_window[window_size - 1] = accepted_num;
    }
}

int is_num(int seqno) {
    for (int i = 0; i < window_size; i++) {
        if (anticipated_window[i] == seqno) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval; /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE]; // Max-segment size
    struct timeval tp;

    /* 
     * check command line arguments 
     */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    // Opening the file
    fp = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

    /* 
     * socket: create the parent socket 
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
     * us rerun the server immediately after we kill it; 
     * otherwise we have to wait about 20 secs. 
     * Eliminates "ERROR on binding: Address already in use" error. 
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);

    int window_counter = 0;
    while (1) {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *)buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        if (!is_num(recvpkt->hdr.seqno)) {
            if (recvpkt->hdr.seqno == anticipated_sequence) {
                if (recvpkt->hdr.data_size == 0) {
                    // End Of File has been reached
                    fclose(fp);
                    sndpkt = make_packet(0);
                    sndpkt->hdr.data_size = -1000;
                    if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
                        error("ERROR in sendto");
                    }
                    break;
                }

                fill_anticipated_window(anticipated_sequence, window_counter);

                gettimeofday(&tp, NULL);
                VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

                fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
                fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
                anticipated_sequence = anticipated_sequence + recvpkt->hdr.data_size;

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
        }
    }

    return 0;
}
