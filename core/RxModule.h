//
// Created by Lorenzo Donini on 02/09/16.
//

#ifndef S3TP_RXMODULE_H
#define S3TP_RXMODULE_H

#include "Buffer.h"
#include "Constants.h"
#include "utilities.h"
#include "StatusInterface.h"
#include <cstring>
#include <map>
#include <trctrl/LinkCallback.h>

#define PORT_ALREADY_OPEN -1
#define PORT_ALREADY_CLOSED -1
#define MODULE_INACTIVE -2
#define CODE_ERROR_CRC_INVALID -3
#define CODE_NO_MESSAGES_AVAILABLE -4
#define CODE_ERROR_PORT_CLOSED -5
#define CODE_ERROR_INCONSISTENT_STATE -6

#define MAX_REORDERING_WINDOW 256

class RxModule: public Transceiver::LinkCallback,
                        PriorityComparator<S3TP_PACKET*> {
public:
    RxModule();
    ~RxModule();

    void startModule(StatusInterface * statusInterface);
    void stopModule();
    int openPort(uint8_t port);
    int closePort(uint8_t port);
    bool isActive();
    bool isNewMessageAvailable();
    void waitForNextAvailableMessage(pthread_mutex_t * callerMutex);
    char * getNextCompleteMessage(uint16_t * len, int * error, uint8_t * port);
    virtual int comparePriority(S3TP_PACKET* element1, S3TP_PACKET* element2);
    void reset();
private:
    bool active;
    Buffer * inBuffer;
    uint8_t to_consume_global_seq;
    uint32_t received_packets;
    pthread_mutex_t rx_mutex;
    pthread_cond_t available_msg_cond;

    StatusInterface * statusInterface;
    std::map<uint8_t, uint8_t> open_ports;
    std::map<uint8_t, uint8_t> current_port_sequence;
    std::map<uint8_t, uint8_t> available_messages;

    // LinkCallback
    void handleFrame(bool arq, int channel, const void* data, int length);
    int handleReceivedPacket(S3TP_PACKET * packet);
    virtual void handleBufferEmpty(int channel);
    void synchronizeStatus(S3TP_SYNC& sync);
    void handleLinkStatus(bool linkStatus);
    bool isPortOpen(uint8_t port);
    bool isCompleteMessageForPortAvailable(int port);
    //void consumeQueue(uint8_t port);
};


#endif //S3TP_RXMODULE_H
