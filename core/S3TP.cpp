//
// Created by Lorenzo Donini on 25/08/16.
//

#include "S3TP.h"

S3TP::S3TP() {
    pthread_mutex_init(&clients_mutex, NULL);
    pthread_mutex_init(&s3tp_mutex, NULL);
    pthread_mutex_lock(&s3tp_mutex);
    reset();
    pthread_mutex_unlock(&s3tp_mutex);
}

S3TP::~S3TP() {
    pthread_mutex_lock(&s3tp_mutex);
    bool toStop = active;
    pthread_mutex_unlock(&s3tp_mutex);
    if (toStop) {
        stop();
    }

    pthread_cond_destroy(&assembly_cond);
    std::map<uint8_t, Client*>::iterator it;
    pthread_mutex_lock(&clients_mutex);
    while ((it = clients.begin()) != clients.end()) {
        pthread_mutex_unlock(&clients_mutex);
        it->second->kill();
        pthread_mutex_lock(&clients_mutex);
    }
    pthread_mutex_unlock(&clients_mutex);
    pthread_mutex_destroy(&clients_mutex);
    pthread_mutex_unlock(&s3tp_mutex);
    pthread_mutex_destroy(&s3tp_mutex);
}

void S3TP::reset() {
    rx.reset();
    tx.reset();
}

int S3TP::init(TRANSCEIVER_CONFIG * config) {
    pthread_mutex_lock(&s3tp_mutex);

    active = true;

    if (config->type == SPI) {
        transceiver = Transceiver::BackendFactory::fromSPI(config->descriptor, rx);
    } else if (config->type == FIRE) {
        transceiver = Transceiver::BackendFactory::fromFireTcp(config->mappings, rx);
    }

    rx.setStatusInterface(this);
    rx.setTransportInterface(this);
    pthread_mutex_unlock(&s3tp_mutex);

    transceiver->start();

    pthread_mutex_lock(&s3tp_mutex);
    rx.startModule();
    tx.startRoutine(rx.link);

    int id = pthread_create(&assembly_thread, NULL, &staticAssemblyRoutine, this);
    LOG_DEBUG(std::string("Assembly Thread (id " + std::to_string(id) + "): START"));

    pthread_mutex_unlock(&s3tp_mutex);

    return CODE_SUCCESS;
}

int S3TP::stop() {
    //Deactivating module
    pthread_mutex_lock(&s3tp_mutex);
    active = false;

    //Stop modules connected to Link Layer
    tx.stopRoutine();
    rx.stopModule();
    pthread_mutex_unlock(&s3tp_mutex);

    //Wait for assembly thread to finish
    pthread_join(assembly_thread, NULL);

    //Stop the transceiver instance
    pthread_mutex_lock(&s3tp_mutex);
    transceiver->stop();
    delete transceiver;
    pthread_mutex_unlock(&s3tp_mutex);

    return CODE_SUCCESS;
}

Client * S3TP::getClientConnectedToPort(uint8_t port) {
    pthread_mutex_lock(&clients_mutex);
    std::map<uint8_t, Client *>::iterator it = clients.find(port);
    if (it == clients.end()) {
        pthread_mutex_unlock(&clients_mutex);
        return NULL;
    }
    Client * cli = it->second;
    pthread_mutex_unlock(&clients_mutex);
    return cli;
}

void S3TP::cleanupClients() {
    pthread_mutex_lock(&clients_mutex);
    for (auto const& port: disconnectedClients) {
        Client * cli = clients[port];
        //Client disconnected from port. Mark that port as available again.
        if (cli != nullptr) {
            clients.erase(cli->getAppPort());
            cli->kill();
            delete cli;
        }
    }
    disconnectedClients.clear();
    pthread_mutex_unlock(&clients_mutex);
}

int S3TP::sendToLinkLayer(uint8_t channel, uint8_t port, void * data, size_t len, uint8_t opts) {
    /* As messages should still be sent out sequentially.
     * There is not need for a separate fragmentation thread, as the
     * job will simply be done by the calling client thread.
     * We first check whether the message can actually be enqueued.
     * If queue is full or link is not active, the message is not accepted and an error is returned.
     * */
    int availability;

    pthread_mutex_lock(&s3tp_mutex);
    bool isActive = active;
    pthread_mutex_unlock(&s3tp_mutex);
    if (isActive) {
        availability = checkTransmissionAvailability(port, channel, (uint16_t)len);
        if (availability != CODE_SUCCESS) {
            return availability;
        }
        if (len > MAX_PDU_LENGTH) {
            //Payload exceeds maximum length: drop packet and return error
            return CODE_ERROR_MAX_MESSAGE_SIZE;
        } else if (len > LEN_S3TP_PDU) {
            //Packet needs fragmentation
            return fragmentPayload(channel, port, data, len, opts);
        } else {
            //Payload fits into one packet
            return sendSimplePayload(channel, port, data, len, opts);
        }
    }
    return CODE_INTERNAL_ERROR;
}

int S3TP::sendSimplePayload(uint8_t channel, uint8_t port, void * data, size_t len, uint8_t opts) {
    S3TP_PACKET * packet;

    //Send to Tx Module without fragmenting
    packet = new S3TP_PACKET((char *)data, (uint16_t) len);
    packet->channel = channel;
    packet->options = opts;
    packet->getHeader()->setPort(port);

    return tx.enqueuePacket(packet, 0, false, channel, opts);
}

int S3TP::fragmentPayload(uint8_t channel, uint8_t port, void * data, size_t len, uint8_t opts) {
    S3TP_PACKET * packet;

    //Need to fragment
    int written = 0;
    int status = CODE_SUCCESS;
    uint8_t fragment = 0;
    bool moreFragments = true;
    char * dataPtr = (char *) data;

    while (written < len) {
        if (written + LEN_S3TP_PDU > len) {
            //We are at the last packet, so don't need to write max payload
            packet = new S3TP_PACKET(dataPtr, (uint16_t)(len - written));
        } else {
            //Filling packet payload with max permitted payload
            packet = new S3TP_PACKET(dataPtr, LEN_S3TP_PDU);
        }
        packet->getHeader()->setPort(port);
        packet->options = opts;
        packet->channel = channel;
        written += packet->getHeader()->getPduLength();
        dataPtr += written;

        if (written >= len) {
            moreFragments = false;
        }
        //Send to Tx Module
        status = tx.enqueuePacket(packet, fragment, moreFragments, channel, opts);
        if (status != CODE_SUCCESS) {
            return status;
        }
        //Increasing current fragment number for next iteration
        fragment++;
    }

    return status;
}

/*
 * Assembly Thread logic
 */
void S3TP::assemblyRoutine() {
    uint16_t len;
    int error;
    char * data;
    Client * cli;
    uint8_t  port;

    pthread_mutex_lock(&s3tp_mutex);
    while(active && rx.isActive()) {
        if (!rx.isNewMessageAvailable()) {
            rx.waitForNextAvailableMessage(&s3tp_mutex);
            continue;
        }
        pthread_mutex_unlock(&s3tp_mutex);
        data = rx.getNextCompleteMessage(&len, &error, &port);
        if (error != CODE_SUCCESS) {
            LOG_WARN("Error while trying to consume message");
        }

        LOG_DEBUG(std::string("Correctly consumed data from queue " + std::to_string((int)port)
                              + " (" + std::to_string(len) + " bytes)"));

        pthread_mutex_lock(&clients_mutex);
        cli = clients[port];
        if (cli != NULL) {
            cli->send(data, len);
            delete[] data;
        } else {
            LOG_WARN(std::string("Port " + std::to_string((int)port)
                                 + " is not open. Couldn't forward data to application"));
        }
        pthread_mutex_unlock(&clients_mutex);

        pthread_mutex_lock(&s3tp_mutex);
    }
    //S3TP (or Rx module) was deactivated
    pthread_mutex_unlock(&s3tp_mutex);

    pthread_exit(NULL);
}

void * S3TP::staticAssemblyRoutine(void * args) {
    static_cast<S3TP*>(args)->assemblyRoutine();
    return NULL;
}

int S3TP::checkTransmissionAvailability(uint8_t port, uint8_t channel, uint16_t msg_len) {
    if (tx.getCurrentState() == TxModule::STATE::BLOCKED) {
        return CODE_LINK_UNAVAIABLE;
    }
    //Computing number of packets that need to be written
    uint8_t no_packets = (uint8_t)(msg_len / LEN_S3TP_PDU);
    if (msg_len % LEN_S3TP_PDU > 0) {
        no_packets += 1;
    }
    //Checking if transmission Q can contain the desired amount of packets
    if (!tx.isQueueAvailable(port, no_packets)) {
        return CODE_QUEUE_FULL;
    } else if (!tx.isChannelAvailable(channel)) {
        //Channel is currently broken
        return CODE_CHANNEL_BROKEN;
    }
    return CODE_SUCCESS;
}

void S3TP::notifyAvailabilityToClients() {
    S3TP_CONNECTOR_CONTROL control;
    control.controlMessageType = AVAILABLE;
    control.error = 0;

    //Notifies all clients
    pthread_mutex_lock(&clients_mutex);
    for (auto const &it : clients) {
        it.second->sendControlMessage(control);
    }
    pthread_mutex_unlock(&clients_mutex);
}

/*
 * Client Interface logic
 */
void S3TP::onApplicationDisconnected(void *params) {
    Client * cli = (Client *)params;
    pthread_mutex_lock(&clients_mutex);
    disconnectedClients.push_back(cli->getAppPort());
    pthread_mutex_unlock(&clients_mutex);
    rx.closePort(cli->getAppPort());
}

void S3TP::onApplicationConnected(void *params) {
    Client * cli = (Client * )params;
    pthread_mutex_lock(&clients_mutex);
    clients[cli->getAppPort()] = cli;
    pthread_mutex_unlock(&clients_mutex);
    rx.openPort(cli->getAppPort());
    //TODO: implement sync
}

int S3TP::onApplicationMessage(void * data, size_t len, void * params) {
    Client * cli = (Client *)params;
    return sendToLinkLayer(cli->getVirtualChannel(), cli->getAppPort(), data, len, cli->getOptions());
}

/*
 * Status callbacks
 */
void S3TP::onLinkStatusChanged(bool active) {
    tx.notifyLinkAvailability(active);
    if (active) {
        //TODO: implement sync in case needed
        notifyAvailabilityToClients();
    }
}

void S3TP::onChannelStatusChanged(uint8_t channel, bool active) {
    tx.setChannelAvailable(channel, active);

    S3TP_CONNECTOR_CONTROL control;
    control.controlMessageType = AVAILABLE;
    control.error = 0;
    //Notify previously blocked clients
    pthread_mutex_lock(&clients_mutex);
    for (auto const &it : clients) {
        if (it.second->getVirtualChannel() == channel) {
            it.second->sendControlMessage(control);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void S3TP::onError(int error, void * params) {
    //TODO: implement
}

void S3TP::onOutputQueueAvailable(uint8_t port) {
    pthread_mutex_lock(&clients_mutex);

    std::map<uint8_t, Client*>::iterator it = clients.find(port);
    if (it != clients.end()) {
        S3TP_CONNECTOR_CONTROL control;
        control.controlMessageType = AVAILABLE;
        control.error = 0;

        it->second->sendControlMessage(control);
    }

    pthread_mutex_unlock(&clients_mutex);
}

/*
 * Transport callbacks
 */
/**
 * Received upon first initialization of remote module. Used only once.
 * The same message is used to acknowledge a remote setup.
 *
 * @param ack  Additional ack flag set in the received setup packet
 */
void S3TP::onSetup(bool ack) {
    pthread_mutex_lock(&s3tp_mutex);
    if (ack) {
        // In case we received an ack for sync
        if (setupInitiated) {
            // We were initiator of setup and got an ack. We still need to ack the ack (3-way last step)
            tx.scheduleSetup(true);
            setupPerformed = true;
            setupInitiated = false;
        } else {
            // We didn't initiate the setup and got back the last ack of the 3-way handshake.
            setupPerformed = true;
        }
    } else {
        // We weren't initiator, we need to send back an ack
        tx.scheduleSetup(true);
        setupPerformed = false;
    }
    pthread_mutex_unlock(&s3tp_mutex);
}

/**
 * Received in case something went wrong on either side.
 * When received, internal state needs to be reset immediately.
 *
 * @param ack  Additional ack flag set in the received reset packet
 */
void S3TP::onReset(bool ack) {
    pthread_mutex_lock(&s3tp_mutex);
    if (ack && resetInitiated) {
        // Reset complete. We're good to go
        resetInitiated = false;
    } else {
        // Received a force reset. Resetting everything, then acknowledging it
        reset();
        tx.scheduleReset(true);
    }
    pthread_mutex_unlock(&s3tp_mutex);
}

void S3TP::onReceivedPacket(uint16_t sequenceNumber) {
    //Send ACK
    tx.scheduleAcknowledgement(sequenceNumber);
}

void S3TP::onReceiveWindowFull(uint16_t lastValidSequence) {
    tx.scheduleAcknowledgement(lastValidSequence);
}

void S3TP::onAcknowledgement(uint16_t sequenceAck) {
    //Notify TX that an ack was received
    tx.notifyAcknowledgement(sequenceAck);
}

//TODO: implement
void S3TP::onConnectionRequest(uint8_t port, uint8_t channel, uint16_t sequenceNumber) {
    rx.openPort(port);
    uint8_t defaultOpts = S3TP_ARQ;
    //TODO: send seq num as well, so we can use it as an ack in piggybacking
    tx.scheduleSync(port, channel, defaultOpts);
}

void S3TP::onConnectionAccept(uint8_t port, uint16_t sequenceNumber) {
    tx.scheduleAcknowledgement(sequenceNumber);
    //TODO: notify client that connection is now open
}

void S3TP::onConnectionClose(uint8_t port) {
    //No 4-way close. We know the connection has been closed and that's it.
    rx.closePort(port);
    //TODO: notify client
}