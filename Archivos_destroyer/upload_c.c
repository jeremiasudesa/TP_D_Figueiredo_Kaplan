#include "common.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define TCP_PORT_UPLOAD 20252

// Parámetros que recibe cada hilo
typedef struct {
  int sockfd;        // socket ya conectado
  size_t buf_size;   // tamaño del buffer de envío
  uint8_t header[6]; // encabezado de 6 bytes
} upload_args_t;

void *upload_thread(void *arg) {
  upload_args_t *args = (upload_args_t *)arg;

  // Enviar header
  send(args->sockfd, args->header, sizeof(args->header), 0);

  // Crear buffer de datos
  uint8_t *buf = malloc(args->buf_size);
  if (!buf) {
    perror("malloc");
    pthread_exit(NULL);
  }
  memset(buf, 0xAA, args->buf_size); // Datos dummy

  // Enviar datos en un bucle
  while (1) {
    ssize_t sent = send(args->sockfd, buf, args->buf_size, 0);
    if (sent < 0) {
      break; // Error al enviar, salir del bucle
    }
  }
  free(buf);   // Liberar el buffer
  return NULL; // Terminar el hilo
}

int client_upload(const char *srv_ip, int N) {
  // Crear sockets TCP
  struct sockaddr_in srv_addr;
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(TCP_PORT_UPLOAD);
  inet_pton(AF_INET, srv_ip, &srv_addr.sin_addr);

  int socks[N];
  for (int i = 0; i < N; i++) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
      die("socket()");
    }
    if (connect(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
      die("connect()");
    }

    socks[i] = sockfd; // Guarda el socket conectado
  }

  // Crear test id
  uint8_t test_id[4];
  do {
    for (int i = 0; i < 4; i++) {
      test_id[i] = (uint8_t)rand();
    }
  } while (test_id[0] == 0xFF); // Asegurarse de que el primer byte no sea 0xFF

  // Crear hilos
  pthread_t threads[N];
  upload_args_t args[N];
  for (int i = 0; i < N; i++) {
    // Crear header
    uint8_t header[6];
    memcpy(header, test_id, 4);  // Copiar test_id
    uint16_t cid = htons(i + 1); // Client ID (1 a N)
    memcpy(header + 4, &cid, 2); // Copiar Client ID al header
    memcpy(args[i].header, header, sizeof(header));

    args[i].sockfd = socks[i];   // Asignar socket
    args[i].buf_size = 8 * 1024; // 8 KiB de datos
    if (pthread_create(&threads[i], NULL, upload_thread, &args[i]) != 0) {
      die("pthread_create()");
    }
  }

  // Esperar a que terminen los hilos
  for (int i = 0; i < N; i++) {
    pthread_join(threads[i], NULL);
    close(socks[i]); // Cerrar socket
  }

  // Falta logica de pedir info al servidor y recibir respuesta
}
