#include "common.h"
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "latency.h"
#include "handle_result.h"

int send_results_udp(int sockfd, uint8_t *resp, results_lock_t *results_lock,
                     struct sockaddr_in client_addr, socklen_t client_addr_len)
{
    pthread_mutex_lock(&results_lock->mutex);
    struct BW_result *bw_results = results_lock->results;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        uint32_t net_resp;
        memcpy(&net_resp, resp, sizeof(net_resp)); // pull the 4 bytes into a uint32_t
        uint32_t resp_id = ntohl(net_resp);        // convert from network (big-endian) into host order
        if (bw_results[i].id_measurement == resp_id)
        {
            // Empaqueta el resultado en el buffer de respuesta
            uint8_t buff[MAX_PAYLOAD];
            int bytes_packed = packResultPayload(bw_results[i], buff, sizeof(buff));
            if (bytes_packed < 0)
            {
                fprintf(stderr, "Error packing result payload\n");
                pthread_mutex_unlock(&results_lock->mutex);
                return -1;
            }

            // Envía el resultado empaquetado al cliente
            if (sendto(sockfd, buff, bytes_packed, 0, (struct sockaddr *)&client_addr, client_addr_len) < 0)
            {
                perror("sendto result");
                pthread_mutex_unlock(&results_lock->mutex);
                return -1;
            }

            printf("Sent result for measurement ID 0x%02X%02X%02X%02X to %s:%d\n",
                   resp[0], resp[1], resp[2], resp[3],
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            memset(&bw_results[i], 0, sizeof(bw_results[i]));
        }
    }
    pthread_mutex_unlock(&results_lock->mutex);
    return 0; // Retorna 0 para indicar éxito
}

void *latency_echo_server(void *args)
{
    echo_server_args_t *echo_args = (echo_server_args_t *)args;
    results_lock_t *results_lock = echo_args->results_lock;

    printf("server: UDP latency service on port %d …\n", UDP_SERVER_PORT);
    struct sockaddr_in srv_addr, client_addr;
    socklen_t addr_len = sizeof(srv_addr), client_addr_len = sizeof(client_addr);
    int sockfd = udp_socket_init(NULL, UDP_SERVER_PORT, &srv_addr, 1);
    if (sockfd < 0)
    {
        fprintf(stderr, "Error initializing UDP socket\n");
        return NULL;
    }

    uint8_t resp[LAT_PAYLOAD_SIZE];
    while (1)
    {
        ssize_t r = recvfrom(sockfd, resp, LAT_PAYLOAD_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);
        printf("Received packet, resp[0]=0x%02X\n", resp[0]);
        if (r < 0)
        {
            perror("recvfrom");
            continue; // Ignora errores de recepción
        }
        if (r != LAT_PAYLOAD_SIZE)
        {
            fprintf(stderr, "Invalid packet received\n");
            continue; // Ignora paquetes inválidos
        }

        // Enviar resultados de Upload si el primer byte no es 0xff
        if (resp[0] != 0xff)
        {
            if (send_results_udp(sockfd, resp, results_lock, client_addr, client_addr_len) < 0)
            {
                fprintf(stderr, "Error sending results\n");
            }
        }

        else
        {
            // Responde con el mismo paquete recibido
            if (sendto(sockfd, resp, LAT_PAYLOAD_SIZE, 0, (struct sockaddr *)&client_addr, addr_len) < 0)
            {
                perror("sendto");
                continue; // Ignora errores de envío
            }
            printf("Echoed packet to %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        }
    }
    close(sockfd);
    return NULL;
}

int client_measure_latency(const char *srv_ip, int tries, int timeout_sec,
                           double *rtts)
{
    struct sockaddr_in srv_addr, from_addr;
    socklen_t addr_len = sizeof(srv_addr), from_len = sizeof(from_addr);
    int sockfd = udp_socket_init(srv_ip, UDP_SERVER_PORT, &srv_addr, 0);
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