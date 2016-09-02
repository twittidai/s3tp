//
// Created by Lorenzo Donini on 31/08/16.
//

#ifndef S3TP_S3TP_CLIENT_H
#define S3TP_S3TP_CLIENT_H

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "../connector/s3tp_shared.h"
#include "s3tp_main.h"

class client {
private:
    pthread_t client_thread;
    pthread_mutex_t client_mutex;
    SOCKET socket;
    bool connected;
    uint8_t app_port;
    uint8_t virtual_channel;
    uint8_t options;
    s3tp_main * main;

    void clientRoutine();
    static void * staticClientRoutine(void * args);
public:
    client(SOCKET socket, S3TP_CONFIG config, s3tp_main * main);
    void kill();
};

#endif //S3TP_S3TP_CLIENT_H