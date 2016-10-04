//
// Created by lorenzodonini on 04.10.16.
//

#ifndef S3TP_TRANSPORTINTERFACE_H
#define S3TP_TRANSPORTINTERFACE_H

class TransportInterface {
    virtual void onSynchronization(uint8_t syncId) = 0;
    virtual void onReceiveWindowFull() = 0;
    virtual void onAcknowledgement(uint8_t sequenceAck) = 0;
};

#endif //S3TP_TRANSPORTINTERFACE_H