#include "common.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "latency.h"

int server_measure_latency(int tries)
{
    struct sockaddr_in srv_addr;
    socklen_t addr_len = sizeof(srv_addr);
    int sockfd = udp_socket_init(NULL, UDP_SERVER_PORT, &srv_addr, 1);
    if (sockfd < 0)
    {
        fprintf(stderr, "Error initializing UDP socket\n");
        return LAT_SOCK_ERR;
    }

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

int client_measure_latency(const char *srv_ip, int tries, int timeout_sec,
                           double *rtts)
{
    struct sockaddr_in srv_addr, from_addr;
    socklen_t addr_len = sizeof(srv_addr), from_len = sizeof(from_addr);
    int sockfd = udp_socket_init(srv_ip, UDP_SERVER_PORT, &srv_addr);
    if (sockfd < 0)
    {
        fprintf(stderr, "Error initializing UDP socket\n");
        return LAT_SOCK_ERR;
    }

    uint8_t payload[LAT_PAYLOAD_SIZE], resp[LAT_PAYLOAD_SIZE];
    struct timeval tv = {.tv_sec = timeout_sec, .tv_usec = 0};
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt SO_RCVTIMEO");
        return LAT_SOCK_ERR;
    }

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
        if (sendto(sockfd, payload, LAT_PAYLOAD_SIZE, 0,
                   (struct sockaddr *)&srv_addr, addr_len) < 0)
        {
            perror("sendto latency");
            return LAT_SEND_ERR;
        }

        ssize_t r = recvfrom(sockfd, resp, LAT_PAYLOAD_SIZE, 0,
                             (struct sockaddr *)&from_addr, &from_len);

        if (r < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                fprintf(stderr, "Timeout de %lds esperando respuesta RTT #%d. Abortando.\n",
                        (long)tv.tv_sec, i + 1);
                return LAT_TIMEOUT_ERR;
            }
            perror("recvfrom latency");
            return LAT_RECV_ERR;
        }
        struct timespec end = now_ts();

        if (r != LAT_PAYLOAD_SIZE ||
            memcmp(payload, resp, LAT_PAYLOAD_SIZE) != 0)
        {
            fprintf(stderr, "Paquete inválido: recibido %zd bytes, contenido no coincide\n", r);
            return LAT_RECV_ERR;
        }
        rtts[i] = diff_ts(&start, &end);

        sleep(1); // Espera 1 segundo entre intentos
    }

    return LAT_OK;
}