//
// Created by Lorenzo Donini on 30/08/16.
//

#ifndef S3TP_S3TP_SHARED_H
#define S3TP_S3TP_SHARED_H

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <iostream>
#include <pthread.h>

#define LOCK(mutex) pthread_mutex_lock(mutex)
#define UNLOCK(mutex) pthread_mutex_unlock(mutex)

#define S3TP_OPTION_ARQ 0x01;
#define S3TP_OPTION_CUSTOM 0x02;

/*
 * Definition or status codes generated locally
 */
#define CODE_ERROR_SOCKET_CREATE -1
#define CODE_ERROR_SOCKET_CONNECT -2
#define CODE_ERROR_SOCKET_BIND -3
#define CODE_ERROR_SOCKET_CONFIG -4
#define CODE_ERROR_SOCKET_NO_CONN -5
#define CODE_ERROR_SOCKET_WRITE -6
#define CODE_ERROR_SOCKET_READ -7
#define CODE_ERROR_LENGTH_CORRUPT -8
#define CODE_ERROR_INVALID_LENGTH -9
#define CODE_SUCCESS 0

/*
 * Definition of status codes generated by s3tp server endpoint
 */
#define CODE_SERVER_ACCEPT 0
#define CODE_SERVER_TRANSMISSION_ERROR -1
#define CODE_SERVER_PORT_BUSY -2
#define CODE_SERVER_INTERNAL_ERROR -3

#define SAFE_TRANSMISSION_COUNT 3

#include "../core/Logger.h"

/*
 * Typedefs
 */
typedef int SOCKET;

typedef void (*S3TP_CALLBACK) (char*, size_t);

typedef struct tag_s3tp_config {
    uint8_t port;
    uint8_t channel;
    uint8_t options;

    void setArq(int active) {
        options ^= (active & 0x01);
    }
}S3TP_CONFIG;

typedef struct tag_s3tp_length_redundant {
    size_t command[SAFE_TRANSMISSION_COUNT];
}S3TP_INTRO_REDUNDANT;

extern const char * socket_path;

/*
 * Utility functions and macros
 */
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MIN(a,b) (((a) > (b)) ? (a) : (b))

int read_length_safe(int fd, size_t * out_length);
int write_length_safe(int fd, size_t len);

#endif //S3TP_S3TP_SHARED_H