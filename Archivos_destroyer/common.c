#define _POSIX_C_SOURCE 199309L

#include "common.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int init_udp_socket(int port, int bind_it) {
  int sd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sd < 0)
    die("socket()");
  if (bind_it) {
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_addr.s_addr = INADDR_ANY,
                               .sin_port = htons(port)};
    if (bind(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
      die("bind()");
  }
  return sd;
}

struct timespec now_ts(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t;
}

double diff_ts(const struct timespec *start, const struct timespec *end) {
  return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

void die(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}