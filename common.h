#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <pthread.h>
#include "config.h" // For MAX_CLIENTS, BW_result

typedef struct results_lock
{
    pthread_mutex_t mutex;
    struct BW_result *results; // Array to store results for each client
} results_lock_t;

int udp_socket_init(const char *srv_ip, int port, struct sockaddr_in *srv_addr, int bind_flag);

struct timespec now_ts(void);

double diff_ts(const struct timespec *start, const struct timespec *end);

void die(const char *msg);

#endif