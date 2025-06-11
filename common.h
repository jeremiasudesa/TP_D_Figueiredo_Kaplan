#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

int udp_socket_init(const char *srv_ip, int port, struct sockaddr_in *srv_addr, int bind_flag);

struct timespec now_ts(void);

double diff_ts(const struct timespec *start, const struct timespec *end);

void die(const char *msg);

#endif