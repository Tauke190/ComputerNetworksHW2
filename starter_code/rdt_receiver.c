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
/*
 * You are required to change the implementation to support
 * window size greater than one.
 * In the current implementation the window size is one, hence we have
 * only one send and receive packet
 */

tcp_packet *recvpkt;
tcp_packet *sndpkt;

int anticipated_sequence = 0; // this is the expected sequence number from the sender side towards receiver 

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
    fp  = fopen(argv[2], "w");
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
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

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
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
                sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    while (1) {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        //VLOG(DEBUG, "waiting from server \n");
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *) buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);



        if (recvpkt->hdr.seqno == anticipated_sequence){ // if the expected sequence number is correct

                if (recvpkt->hdr.ctr_flags == -1000) { // check for end of file, if end of file has been reached then this block of code sends 100 eof flags to the receiver.
                // my choice for 100 is because I want to be sure that even during extreme losss, atleast one signal gets picked up
                    VLOG(INFO, "End Of File has been reached");
                    fclose(fp);
                    tcp_packet* last_packet = make_packet(0);
                    last_packet->hdr.ctr_flags = -1000;
                    int counter = 0;
                // sedning 101 EOF flags. 
                    while (counter < 101){
                        int send_packet = sendto(sockfd, last_packet, TCP_HDR_SIZE, 0, (const struct sockaddr *) &clientaddr, clientlen);
                        if (send_packet < 0) {
                            error("Error sending the end of file packet");
                        }
                        counter++;
                    }

                    break;
                }
                
    
                gettimeofday(&tp, NULL);
                VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

                fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
                fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
                anticipated_sequence = anticipated_sequence + recvpkt->hdr.data_size; // updated the expected sequence number for the next iteration
                
                // sending an Ack for the packet. 
                sndpkt = make_packet(0); // make a packet with data size 0 because this is our ACK that we are sending 
                sndpkt->hdr.ackno = anticipated_sequence; 
                sndpkt->hdr.ctr_flags = ACK;
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                        (struct sockaddr *) &clientaddr, clientlen) < 0) {
                    error("ERROR in sendto");
                }
    



        } else {
                
                // if its not anticipated sequence number, then just discard the packet, and send the latest Ack with expecteded sequence number which is the anticipated sequence number
                sndpkt = make_packet(0);
                sndpkt->hdr.ackno = anticipated_sequence;
                sndpkt->hdr.ctr_flags = ACK;
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                        (struct sockaddr *) &clientaddr, clientlen) < 0) {
                    error("ERROR in sendto");
                }


        }
   
       
    }

    return 0;
}
