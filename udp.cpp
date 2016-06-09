/* 
 * @file   udp.cpp
 * @brief  Implements client and server functions that use stop-and-wait and
 *          sliding window mechanisms to ensure reliable, orderly deliver of
 *          network frames over UDP.
 * @author brendan
 * @date   October 25, 2012
 */

#include "UdpSocket.h"
#include "Timer.h"

static const long MAX_TIME = 1500;

int ackAdvance(UdpSocket &sock, int lastSeqRec, int windowSize);


/**
 * Sends message[] and receives an acknowledgment from the server max (=20,000)
 *  times using the sock object. If the client cannot receive an acknowledgment
 *  immediately, it should start a Timer. If a timeout occurs (i.e., no
 *  response after 1500 usec), the client must resend the same message. The
 *  function must count the number of messages retransmitted and return it to
 *  the main function as its return value (McCarthy).
 * @param  sock  bound UDP socket for data transfer.
 * @param  max  number of messages to be transmitted.
 * @param  message  a message to transmit; only first element is relevant. 
 * @pre    sock has been established; serverReliable() is given the same max.
 * @post   All messages have been sent and an ack has been received for each.
 * @return A count of the number of messages that were transmitted more than
 *          once.
 */
int clientStopWait(UdpSocket &sock, const int max, int message[]) {
    int   retrans = 0;  // counter for retransmission of messages
    int   seqNum  = 1;  // 1-bit sequence number for acks
    Timer timeout;      // timer to guage need for retransmission
    
    // perform at least max sendTo and recvFrom operations
    for (int msgNum = 0; msgNum < max; ++msgNum) {
        message[0] = msgNum & 1;        // set 1-bit sequence number
        
        do {    // send the message until proper acknowledgement is received
            sock.sendTo((char*)message, sizeof(message));
            timeout.start();    // start timer outside wait loop
            // wait for a reply
            while(sock.pollRecvFrom() < 1) {
                if (timeout.lap() > MAX_TIME) {
                    // after timeout, resend message and restart timer
                    sock.sendTo((char*)message, sizeof(message));
                    ++retrans;
                    timeout.start();
                } // end if (timeout.lap() > MAX_TIME)
            } // end while(sock.pollRecvFrom() < 1)
            sock.recvFrom((char*)&seqNum, sizeof(int));
        // if acknowledgement is wrong, increment retransmit counter and loop
        retrans += seqNum ^ message[0];
        } while(seqNum != message[0]);
    } // end for (; msgNum < max; )
    
    return retrans;
} // end clientStopWait(UdpSocket&, const int, int[])


/**
 * Repeats receiving message[] and sending an acknowledgment at a server side
 *  max (=20,000) times using the sock object (McCarthy).
 * @param  sock  bound UDP socket for data transfer.
 * @param  max  number of messages to be received.
 * @param  message  a message to retrieve; only first element is relevant.
 * @pre    sock has been established; clientStopWait() is given the same max.
 * @post   All received messaged have been ack'd in the correct order.
 */
void serverReliable(UdpSocket &sock, const int max, int message[]) {
    // perform at least max recvFrom and ackTo operations
    for (int msgToAck = 0; msgToAck < max; ++msgToAck) {
        do {    // blocking receive should work on this server
            sock.recvFrom((char*)message, sizeof(message));
            sock.ackTo((char*)&message[0], sizeof(int));
        // if sequence number is not expected msgToAck, try again
        } while(message[0] != msgToAck & 1);
    } // end for (; msgToAck < max; )
} // end serverReliable(UdpSocket&, const int, int[])


/**
 * Sends message[] and receives an acknowledgment from a server max (=20,000)
 *  times using the sock object. The client can continuously send a new
 *  message[] and increment the sequence number as long as the number of in-
 *  transit messages (i.e., number of unacknowledged *  messages) is less than
 *  windowSize. That number should be decremented every time the client
 *  receives an acknowledgment. If the number of unacknowledged messages
 *  reaches windowSize, the client should start a Timer. If a timeout occurs
 *  (i.e., no response after 1500 usec), it must resend the message with the
 *  minimum sequence number among those which have not yet been acknowledged.
 *  The function must count the number of messages retransmitted and return it
 *  to the main function as its return value (McCarthy).
 * @param  sock  bound UDP socket for data transfer.
 * @param  max  number of messages to be transmitted.
 * @param  message  a message to transmit; only first element is relevant. 
 * @param  windowSize  number of sent messages that can be buffered before an
 *                      ack must be received.
 * @pre    sock has been established; serverEarlyRetrans() is given the same
 *          max and windowSize.
 * @post   All messages have been sent and an ack has been received for each.
 * @return A count of the number of messages that were transmitted more than
 *          once.
 */
int clientSlidingWindow(UdpSocket &sock, const int max,
                         int message[], int windowSize) {
    int   retrans       = 0;    // counter for retransmission of messages
    int   lastAckRec    = 0;    // index of last ack; nothing received yet
    int   lastFrameSent = 0;    // index of last message; nothing sent yet
    Timer timeout;              // timer to guage need for retransmission
    int   buffer[(windowSize + 1) * max];       // sent message queue
    int   seqRange = windowSize * 2 + 1;        // range for sequence numbers
    
    // perform max acknowledged send operations
    for (int msgNum = 0; msgNum < max; ++msgNum) {
        timeout.start();
        // check if buffer is full, wait if it is
        while(lastAckRec == (lastFrameSent + 1) % (windowSize + 1)) {
            if (timeout.lap() > MAX_TIME) {
                // after timeout, resend all queued messages and restart timer
                for (int i = 1;
                         i <= (lastFrameSent - lastAckRec + windowSize + 1) %
                               (windowSize + 1); ++i) {
                    sock.sendTo((char*)buffer[((lastAckRec + i) %
                                    (windowSize + 1)) * max], sizeof(message));
                    ++retrans;
                } // end for (; i <= (lastFrameSent -...; )
                
                timeout.start();
            } // end if (timeout.lap() > MAX_TIME)
            // try to advance head of queue
            lastAckRec = (lastAckRec + ackAdvance(sock, buffer[(lastAckRec + 1)
                          * max], windowSize)) % (windowSize + 1);
        } // end while(lastAckRec == (lastFrameSent + 1)...)
        // prepare and send message, advance back of queue
        message[0] = msgNum % seqRange;
        sock.sendTo((char*)message, sizeof(message));
        lastFrameSent = (lastFrameSent + 1) % (windowSize + 1);
        // copy message into buffer
        for (int i = 0; i < sizeof(message) / sizeof(int); ++i) {
            buffer[lastFrameSent * max + i] = message[i];
        } // end for (; i < sizeof(message) / sizeof(int); )
        // try to advance head of queue
        lastAckRec = (lastAckRec + ackAdvance(sock, buffer[((lastAckRec + 1) %
                     (windowSize + 1)) * max], windowSize)) % (windowSize + 1);
    } // end for (; msgNum < max; )
    
    return retrans;
} // end clientSlidingWindow(UdpSocket&, const int, int[], int)


/**
 * Determines how far to advance the last frame ack'd. Since a cumulative ack
 *  is expected, the advance can be as large as windowSize. If there is no ack
 *  ready, the advance will be 0.
 * @param  sock  bound UDP socket for data transfer.
 * @param  lastSeqRec  the last sequence number to that has been ack'd.
 * @param  windowSize  measure from lastSeqRec that is acceptable for new ack.
 * @pre    sock has been established.
 * @post   None.
 * @return The distance between the last ack'd frame and the currently ack'd
 *          frame; 0 <= return <= windowSize.
 */
int ackAdvance(UdpSocket &sock, int lastSeqRec, int windowSize) {
    int recAckNum;      // container for received ack
    int seqRange = windowSize * 2 + 1;      // max allowed sequence number
    
    if (sock.pollRecvFrom() > 0) {
        // receive acknowledgment from server
        sock.recvFrom((char*)&recAckNum, sizeof(int));
        // ensure recieved ack is within expected range
        if ((recAckNum - (lastSeqRec + 1) + seqRange) % seqRange < windowSize){
            return ((recAckNum - lastSeqRec + seqRange) % seqRange);
        } // end if ((recAckNum - (lastSeqRec + 1)...)
    } // end if (sock.pollRecvFrom() > 0)
    // if the sequence number of the ack is out of range, no advance
    return 0;
} // end checkMsg


/**
 * Receives message[] and sends an acknowledgment to the client max (=20,000)
 *  times using the sock object. Every time the server receives a new
 *  message[], it must save the message's sequence number in its array and
 *  return a cumulative acknowledgment (McCarthy).
 * @param  sock  bound UDP socket for data transfer.
 * @param  max  number of messages to be received.
 * @param  message  a message to retrieve; only first element is relevant.
 * @param  windowSize  number of received messages that can be buffered before
 *                      any acks are sent.
 * @pre    sock has been established; clientStopWait() is given the same max.
 * @post   All received messaged have been ack'd in the correct order.
 */
void serverEarlyRetrans(UdpSocket &sock, const int max,
                             int message[], int windowSize) {
    int seqRange        = windowSize * 2 + 1;   // max allowed sequence number
    int largestAccFrame = windowSize - 1;       // accept up to edge of window
    int lastAckSent     = seqRange - 1;         // set to end of range
    int offset          = 0;                    // for boundary checking
    bool buffer[seqRange];                      // index is the sequence number
    // no sequence numbers encountered, initialize buffer to empty
    for (int i = 0; i < seqRange; ++i) {
        buffer[i] = false;
    } // end for (; i < windowSize; )
    
    // perform at least max receive and acknowledge operations
    for (int msgToAck = 0; msgToAck < max; ++msgToAck) {
        do {    // go until something can be ack'd or buffered
            // receive a message and determine its position in recieve buffer
            sock.recvFrom((char*)message, sizeof(message));
            offset = windowSize -
                      (seqRange + largestAccFrame - message[0]) % seqRange;
            // ensure sequence number is within expected range
            if (offset > 0) {
                buffer[message[0]] = true;
            } // end if (offset > 0)
            // check queue for highest ack to send
            while(buffer[(lastAckSent + 1) % seqRange] == true) {
                buffer[lastAckSent] = false;
                lastAckSent     = (lastAckSent + 1) % seqRange;
                largestAccFrame = (largestAccFrame + 1) % seqRange;
            } // end while(buffer[(lastAckSent + 1)...)
            // update and send next expected sequence number
            message[0] = (lastAckSent + 1) % seqRange;
            sock.ackTo((char*)&message[0], sizeof(int));
        } while(offset <= 0);
    } // end for (; msgToAck < max; )
} // end serverEarlyRetrans(UdpSocket&, const int, int[], int)
