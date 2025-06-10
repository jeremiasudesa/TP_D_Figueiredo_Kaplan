#define _POSIX_C_SOURCE 199309L

#include "common.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int udp_socket_init(const char *srv_ip, int port, struct sockaddr_in *srv_addr, int bind_flag)
{
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    perror("socket");
    return -1;
  }

  memset(srv_addr, 0, sizeof(struct sockaddr_in));
  srv_addr->sin_family = AF_INET;
  srv_addr->sin_port = htons(port);

  if (srv_ip == NULL)
  {
    srv_addr->sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces
  }
  else
  {
    if (inet_pton(AF_INET, srv_ip, &srv_addr->sin_addr) <= 0)
    {
      perror("inet_pton");
      close(sockfd);
      return -1;
    }
  }

  if (bind_flag)
  {
    if (bind(sockfd, (struct sockaddr *)srv_addr, sizeof(*srv_addr)) < 0)
    {
      perror("bind");
      close(sockfd);
      return -1;
    }
  }
  return sockfd;
}

struct timespec now_ts(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t;
}

double diff_ts(const struct timespec *start, const struct timespec *end)
{
  return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1e9;
}

void die(const char *msg)
{
  perror(msg);
  exit(EXIT_FAILURE);
}