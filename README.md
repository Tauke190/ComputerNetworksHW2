# Implementation of a Simplified Transmission Control Protocol (TCP)

This project was completed as part of the Computer Networks course at NYU Abu Dhabi during the summer of 2024, under the guidance of Professor Yasir Zaki. The team members for this project are Aadim Nepal and Avinash Gyawali.

## Getting Started

To run this project, first clone the following repository:

```
git clone https://github.com/Tauke190/ComputerNetworksHW2.git
```

Then, navigate to the `starter_code` directory and run the following command to compile the project:

```
make
```

After this, the files will be compiled to the `objects` directory in the root folder. Navigate to the `objects` directory and then to the respective `sender` and `receiver` directories to run the sender and receiver.

### Running the Receiver

First, run the receiver with the following command:

```
./rdt_receiver 5454 <filename.extension>
```

### Running the Sender

Open a new terminal, navigate to the `sender` directory, and set up the network conditions using Mahi Mahi:

```
mm-delay <num> mm-uplink <num> mm-downlink <num>
```

We assume that you have Mahi Mahi installed. Then run the following command:

```
./rdt_sender $MAHIMAHI_BASE 5454 <filename.extension>
```

## Features

This version of TCP comes with the following features:

1. **Window Size and Protocol**: 
   - The protocol uses a window size of 10 and implements a Go-Back-N protocol.
   
2. **Window Sliding**: 
   - Every time a packet is received, the window size shifts to the left by 1.
   
3. **Sequence Numbers and Acknowledgments**: 
   - Each packet sent contains a sequence number.
   - When a packet is received, an acknowledgment (ACK) is sent back with the expected sequence number for the next packet.
   
4. **Packet Loss and Timeout**: 
   - If a packet is lost and not received by the receiver, a timeout mechanism triggers the retransmission of all packets in the current window.
   
5. **Handling Missing ACKs**: 
   - If an ACK is not received on the sender side, a mechanism is in place to handle this.
   - If the ACK received is numbered higher than the expected ACK, it indicates that all previous packets have been received, and their ACKs just arrived.
   - This higher ACK number automatically acknowledges all packets received so far.
   
6. **End-of-File (EOF) Handling**: 
   - To handle high network delay conditions, an EOF flag is sent 101 times from both the sender and receiver sides.
   - This ensures that even if some EOF flags are lost, at least one will be received and acknowledged, ensuring the termination of the transfer.
   
7. **Transfer Time and Network Conditions**: 
   - The time taken for file transfer depends on the network delay and uplink/downlink loss.
   - With low downlink loss, there may be no need for retransmissions as higher ACKs can acknowledge all preceding packets.
   - As uplink and downlink loss increase, the time taken for packet transfer will also increase.
