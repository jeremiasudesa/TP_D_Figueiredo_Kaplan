#include "common.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "latency.h"

int server_measure_latency(int sockfd, int tries)
{
    uint8_t resp[LAT_PAYLOAD_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int sent = 0;

    while (sent < tries)
    {
        ssize_t r = recvfrom(sockfd, resp, LAT_PAYLOAD_SIZE, 0,
                             (struct sockaddr *)&client_addr, &addr_len);

        if (r < 0)
        {
            perror("recvfrom");
            return LAT_RECV_ERR;
        }

        if (r != LAT_PAYLOAD_SIZE || resp[0] != 0xff)
        {
            fprintf(stderr, "Invalid packet received\n");
            continue; // Ignora paquetes inválidos
        }

        if (sendto(sockfd, resp, LAT_PAYLOAD_SIZE, 0,
                   (struct sockaddr *)&client_addr, addr_len) < 0)
        {
            perror("sendto");
            return LAT_SEND_ERR;
        }
        sent++;
    }
    return LAT_OK;
}

int client_measure_latency(int sockfd, int tries, int timeout_sec,
                           double *rtts)
{
    uint8_t payload[LAT_PAYLOAD_SIZE], resp[LAT_PAYLOAD_SIZE];
    struct timeval tv = {.tv_sec = timeout_sec, .tv_usec = 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    for (int k = 0; k < tries; ++k)
        rtts[k] = -1.0; // Inicializa RTTs a -1.0

    for (int i = 0; i < tries; i++)
    {
        // Primer byte fijo a 0xff según spec
        payload[0] = 0xff;
        // Bytes 1–3 aleatorios
        for (int j = 1; j < LAT_PAYLOAD_SIZE; ++j)
            payload[j] = (uint8_t)rand();

        struct timespec start = now_ts();
        if (send(sockfd, payload, LAT_PAYLOAD_SIZE, 0) < 0)
        {
            perror("send");
            return LAT_SEND_ERR;
        }

        ssize_t r = recv(sockfd, resp, LAT_PAYLOAD_SIZE, 0);
        struct timespec end = now_ts();

        if (r == LAT_PAYLOAD_SIZE && memcmp(payload, resp, LAT_PAYLOAD_SIZE) == 0)
        {
            rtts[i] = diff_ts(&start, &end);
        }
        else
        {
            return LAT_RECV_ERR;
        }

        sleep(1); // Espera 1 segundo entre intentos
    }

    return LAT_OK;
}