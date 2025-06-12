#ifndef UPLOAD_H
#define UPLOAD_H

#include <stdint.h>
#include "common.h"
#include "handle_result.h"

#define TCP_PORT_UPLOAD 20252
#define UDP_PORT_RESULTS 20251
#define MAX_PAYLOAD (8 * 1024)

// Resultado de cada conexión de subida
typedef struct
{
    uint8_t test_id[4];  // Identificador de prueba
    uint16_t conn_id;    // Identificador de conexión (1…N)
    uint64_t bytes_recv; // Total de bytes leídos
    double duration;     // Segundos efectivos de lectura
} upload_result_t;

// Parámetros para cada hilo del cliente de subida
typedef struct
{
    int sockfd;        // Socket ya conectado
    size_t buf_size;   // Tamaño del buffer de envío
    uint8_t header[6]; // Encabezado de 6 bytes (test_id + conn_id)
} cli_thread_arg_t;

// Parámetros para cada hilo del servidor de subida
typedef struct
{
    int conn_fd;           // Descriptor del socket de escucha
    upload_result_t *res;  // Puntero donde escribir el resultado
    struct timespec start; // Tiempo de inicio de la conexión
    int T;                 // Tiempo total de la conexión en segundos
} srv_thread_arg_t;

// Atiende una conexión TCP de subida en el servidor
void *upload_server_thread(void *arg);

// Inicia el servidor de subida TCP, lanza N hilos y envía resultados por UDP
int server_upload(int N, int T, results_lock_t *results_lock);

// Envía datos al servidor en el cliente de subida
void *upload_client_thread(void *arg);

// Inicia el cliente de subida TCP, lanza N hilos y recibe resultados UDP
// Retorna el número total de bytes enviados
int client_upload(const char *srv_ip, int N, struct BW_result *bw_result);

#endif // UPLOAD_H
