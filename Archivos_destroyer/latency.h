#ifndef LATENCY_H
#define LATENCY_H

#include <stdint.h>
#include "common.h"

#define LAT_PAYLOAD_SIZE 4 // tamaño fijo del payload de latencia
#define LAT_OK 0           // sin error
#define LAT_SEND_ERR 1     // error al enviar
#define LAT_RECV_ERR 2     // error al recibir

// Atiende peticiones de latencia en el servidor
int server_measure_latency(int sockfd, int tries);

// Mide RTT desde el cliente
int client_measure_latency(int sockfd, int tries, int timeout_sec, double *rtts);

#endif // LATENCY_H
