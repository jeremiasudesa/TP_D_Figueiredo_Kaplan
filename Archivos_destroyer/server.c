#include "common.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define LAT_PAYLOAD_SIZE 4
#define LAT_OK 0
#define LAT_SEND_ERR 1
#define LAT_RECV_ERR 2

int server_measure_latency(int sockfd, int tries) {
  uint8_t resp[LAT_PAYLOAD_SIZE];
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);
  int sent = 0;

  while (sent < tries) {
    ssize_t r = recvfrom(sockfd, resp, LAT_PAYLOAD_SIZE, 0,
                         (struct sockaddr *)&client_addr, &addr_len);

    if (r < 0) {
      perror("recvfrom");
      return LAT_RECV_ERR;
    }

    if (r != LAT_PAYLOAD_SIZE || resp[0] != 0xff) {
      fprintf(stderr, "Invalid packet received\n");
      continue; // Ignora paquetes inválidos
    }

    if (sendto(sockfd, resp, LAT_PAYLOAD_SIZE, 0,
               (struct sockaddr *)&client_addr, addr_len) < 0) {
      perror("sendto");
      return LAT_SEND_ERR;
    }
    sent++;
  }
  return LAT_OK;
}