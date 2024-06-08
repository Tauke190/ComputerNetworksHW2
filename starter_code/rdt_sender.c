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
#define RETRY       120 // milliseconds
#define WINDOW_SIZE 10

int next_seqno = 0;
int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt[WINDOW_SIZE] = {NULL};
tcp_packet *recvpkt = NULL;
sigset_t sigmask;

void fix_buffer_window() {
    // Free the memory of the first packet
    if (sndpkt[0] != NULL) {
        free(sndpkt[0]);
    }
    // Shift all packets to the left
    for (int i = 1; i < WINDOW_SIZE; i++) {
        sndpkt[i - 1] = sndpkt[i];
    }

    // Set the last position to NULL
    sndpkt[WINDOW_SIZE - 1] = NULL;
}

void resend_packets(int sig) {
    if (sig == SIGALRM) {
        VLOG(INFO, "Timeout happened");
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (sndpkt[i] != NULL) {
                int send_packet = sendto(sockfd, sndpkt[i], TCP_HDR_SIZE + get_data_size(sndpkt[i]), 0, (const struct sockaddr *) &serveraddr, serverlen);
                if (send_packet < 0) {
                    error("Error sending packet");
                }
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
    signal(SIGALRM, sig_handler);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

int main(int argc, char **argv) {
    int portno;
    size_t len;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
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

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* initialize server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* convert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    int window_counter = 0;
    int break_flag = 0;
    int expected_ack_no = next_seqno + TCP_HDR_SIZE;

    while (1) {
        
        while (window_counter < WINDOW_SIZE) {
            len = fread(buffer, 1, DATA_SIZE, fp);
            if (len <= 0) {
                VLOG(INFO, "End of file has been reached");
                tcp_packet* last_packet = make_packet(0);
                last_packet->hdr.seqno = next_seqno;
                int send_packet = sendto(sockfd, last_packet, TCP_HDR_SIZE + get_data_size(last_packet), 0, (const struct sockaddr *) &serveraddr, serverlen);
                if (send_packet < 0) {
                    error("Error sending the end of file packet");
                }
                free(last_packet);
                break_flag = 1;
                break;
            }

            tcp_packet* new_packet = make_packet(len);
            memcpy(new_packet->data, buffer, len);
            new_packet->hdr.seqno = next_seqno;
            sndpkt[window_counter] = new_packet;
            
            int send_packet = sendto(sockfd, new_packet, TCP_HDR_SIZE + get_data_size(new_packet), 0, (const struct sockaddr *) &serveraddr, serverlen);
            if (send_packet < 0) {
                error("Error sending packet");
            }

            next_seqno += len;
            window_counter++;
        }
         if (break_flag == 1){
            break;
        }
        start_timer();

        int receive_packet;
        do {
            receive_packet = recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen);
            if (receive_packet < 0) {
                error("Error receiving packet");
            }
            recvpkt = (tcp_packet *)buffer;
        } while (recvpkt->hdr.ackno != expected_ack_no);

        stop_timer();

       
        if (recvpkt->hdr.ackno == expected_ack_no) {
            fix_buffer_window();
            window_counter--;
        }
    }

    // Close socket and file, clean up resources
    close(sockfd);
    fclose(fp);

    return 0;
}