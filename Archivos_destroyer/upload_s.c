#include "common.h"
#include "handle_result.c"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define TCP_PORT_UPLOAD 20252
#define UDP_PORT_RESULTS 20251

// Estructura donde se almacena el resultado de cada conexión
typedef struct {
  uint8_t test_id[4];  // identificador de prueba
  uint16_t conn_id;    // identificador de conexión (1…N)
  uint64_t bytes_recv; // total de bytes leídos
  double duration;     // segundos efectivos de lectura              // segundos
                       // totales de la conexión
} upload_result_t;

// Estructura interna para pasar args al hilo
typedef struct {
  int conn_fd;
  upload_result_t *res; // puntero a donde escribir el resultado
  int T;                // tiempo total de la conexión en segundos
} upload_thread_arg_t;

// Función que ejecuta cada hilo de subida
void *upload_server_thread(void *arg) {
  upload_thread_arg_t *args = arg;
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  // Aceptar la conexión entrante
  int conn_fd =
      accept(args->conn_fd, (struct sockaddr *)&client_addr, &client_addr_len);
  if (conn_fd < 0) {
    perror("accept");
    pthread_exit(NULL);
  }
  struct timespec start = now_ts();

  // Leer cabecero de 6 bytes
  uint8_t header[6];
  ssize_t r = recv(conn_fd, header, sizeof(header), 0);
  if (r < 0) {
    perror("recv header");
    close(conn_fd);
    pthread_exit(NULL);
  }

  memcpy(args->res->test_id, header, 4);
  args->res->conn_id = ntohs(*(uint16_t *)(header + 4));
  args->res->bytes_recv = r;

  // Leer datos hasta que se cierre la conexión
  uint8_t buf[8 * 1024]; // buffer de 8 KB
  struct timespec now;
  while (1) {
    now = now_ts();
    if (difftime(&start, &now) >= args->T) {
      break; // salir si se ha alcanzado el tiempo total
    }

    r = recv(conn_fd, buf, sizeof(buf), 0);
    if (r <= 0) {
      break; // error o cierre de conexión
    }
    args->res->bytes_recv += r; // acumular bytes leídos
  }

  args->res->duration = diff_ts(&start, &now);
  close(conn_fd);     // cerrar la conexión
  pthread_exit(NULL); // terminar el hilo
}

int server_upload(upload_result_t *res, int N, int T) {
  // Crear socket TCP
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    return -1;
  }

  // Configurar dirección del servidor
  struct sockaddr_in srv_addr;
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_addr.s_addr = INADDR_ANY; // Aceptar conexiones de cualquier IP
  srv_addr.sin_port = htons(TCP_PORT_UPLOAD);

  // Enlazar el socket al puerto
  if (bind(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
    perror("bind");
    close(sockfd);
    return -1;
  }

  // Escuchar conexiones entrantes
  if (listen(sockfd, N) < 0) {
    perror("listen");
    close(sockfd);
    return -1;
  }

  pthread_t threads[N];
  upload_thread_arg_t args[N];

  for (int i = 0; i < N; i++) {
    args[i].conn_fd = sockfd;
    args[i].res = &res[i];
    args[i].T = T; // Tiempo total de la conexión en segundos

    // Crear hilo para manejar la conexión
    if (pthread_create(&threads[i], NULL, upload_server_thread, &args[i]) !=
        0) {
      perror("pthread_create");
      close(sockfd);
      return -1;
    }
  }

  // Esperar a que terminen todos los hilos
  for (int i = 0; i < N; i++) {
    pthread_join(threads[i], NULL);
  }

  close(sockfd); // Cerrar el socket del servidor

  // Agrupar resultados
  struct BW_result bw_result;
  bw_result.id_measurement = (uint32_t)res[0].test_id;

  for (int i = 0; i < N; i++) {
    bw_result.conn_bytes[i] = res[i].bytes_recv;
    bw_result.conn_duration[i] = res[i].duration;
  }

  // Enviar resultados al cliente UDP
  int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_sock < 0) {
    perror("socket UDP");
    return -1;
  }

  struct sockaddr_in udp_addr = {.sin_family = AF_INET,
                                 .sin_port = htons(UDP_PORT_RESULTS),
                                 .sin_addr.s_addr = INADDR_ANY};
  if (bind(udp_sock, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
    perror("bind UDP");
    close(udp_sock);
    return -1;
  }
}