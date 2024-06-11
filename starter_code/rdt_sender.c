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


// this is a function that is called when the timer goes out
// it resends all packets in the current buffer window i.e. sndpkt array
void resend_packets(int sig) {
    if (sig == SIGALRM) {
        VLOG(INFO, "Timeout happened");
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (sndpkt[i] != NULL) {
                VLOG(DEBUG, "Resending packet %d, (%d)", sndpkt[i]->hdr.seqno, (int)sndpkt[i]->hdr.seqno / (int)DATA_SIZE);
                int send_packet = sendto(sockfd, sndpkt[i], TCP_HDR_SIZE + get_data_size(sndpkt[i]), 0, (const struct sockaddr *) &serveraddr, serverlen);
                if (send_packet < 0) {
                    error("Error sending packet");
                }
            }
        }
    }
}

// function to start the timer

void start_timer() {
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

// function to stop the timer
void stop_timer() {
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

// function to intialize the timer
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
    next_seqno = 0; // tracker for next sequence number
    int window_counter = 0; // tracker for where the current window variable is supposed to be
    int expected_ack_no = 0; // expected ack from the receiver side
    int termination_flag = 0; // a tracket to terminate the code

    // Lets start an infinite  loop. 

    while (1) {
        // loop while window_counter is less than window_size, and there is data to read from the file
        while (window_counter < WINDOW_SIZE && (len = fread(buffer, 1, DATA_SIZE, fp)) > 0) {

            // make a new packet to send to receiver with size len
            // update the sequence number accordinagly. 

            tcp_packet* new_packet = make_packet(len);
            memcpy(new_packet->data, buffer, len);
            new_packet->hdr.seqno = next_seqno;
            sndpkt[window_counter] = new_packet;

            VLOG(DEBUG, "Sending packet %d (%d) to %s", next_seqno, (int)next_seqno / (int)DATA_SIZE, inet_ntoa(serveraddr.sin_addr));
            
            // send the packet to the receiver
            int send_packet = sendto(sockfd, new_packet, TCP_HDR_SIZE + get_data_size(new_packet), 0, (const struct sockaddr *) &serveraddr, serverlen);
            if (send_packet < 0) {
                error("Error sending packet");
            }

            // update the next sequence number and window counter is incremented

            next_seqno += len;
            window_counter++;
        }

        // update the expected ack no from the receiver end which is the last unacked packet.

        if (sndpkt[0] != NULL){
            expected_ack_no = sndpkt[0]->hdr.seqno + sndpkt[0]->hdr.data_size;
        }

        // start a timer to receive

        start_timer();

        int receive_packet;
        do {
            // receive a packet from the receiver end
            receive_packet = recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen);
            if (receive_packet < 0) {
                error("Error receiving packet");
            }
            recvpkt = (tcp_packet *)buffer;
            // if the ctr flag is -1000, this is indicating end of file, break out of the loop and set the termination flag
            // to break out of another loop
            if (recvpkt->hdr.ctr_flags == -1000){
                termination_flag = 1;
                VLOG(INFO, "End of file has been reached");
                break;
            }
            // if Ack received is greater than expected Ack,
            // then this indicates that all the Acks before this one has been acknowledges
            // only that the previous Acks were lost
            // so we acknowledge all packets with this Ack value and update all the variable accoridngly

            if (recvpkt->hdr.ackno > expected_ack_no){
                int shift_count = (recvpkt->hdr.ackno - expected_ack_no) / DATA_SIZE;
                for (int i = 0; i < shift_count; i++) {
                    fix_buffer_window();
                    window_counter--;
                }
                
                if (sndpkt[0] != NULL){
                    expected_ack_no = sndpkt[0]->hdr.seqno + sndpkt[0]->hdr.data_size;
                }else{
                    expected_ack_no = recvpkt->hdr.ackno;
                }
                break;
            }
        } while (recvpkt->hdr.ackno != expected_ack_no); // we expect the received Ack to be equal to expected Ack

        stop_timer(); // if received stop the timer and restart it again


        // if termination flag is 1, then this is indicating EOF
        // break out of the entire loop
        if (termination_flag == 1){
            break;
        }

        // if you receive the end of file, then make a packet and send the EOF flag to the receiver
        // again I want this to terminate so even during high network delay, we want to make sure that the termination flag is porcessed to the other end
        // this is why I sent 100 EOF flags, hoping atleast one gets picked up

        if (len <= 0) {
            tcp_packet* last_packet = make_packet(0);
            last_packet->hdr.seqno = next_seqno;
            last_packet->hdr.ctr_flags = -1000;
            int counter = 0;
            while (counter < 101){
                int send_packet = sendto(sockfd, last_packet, TCP_HDR_SIZE + get_data_size(last_packet), 0, (const struct sockaddr *) &serveraddr, serverlen);
                if (send_packet < 0) {
                    error("Error sending the end of file packet");
                }
                counter++;
            }
            free(last_packet);
        }

        // if you received the packet as you expected, shift the buffer window to the left and decrement window_counter to accomodate a space in the buffer


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
