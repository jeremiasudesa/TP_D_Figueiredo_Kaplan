#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> // Added for malloc, free, randa
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "upload.h"
#include "handle_result.h"
#include "config.h"
#include <unistd.h>

void *upload_server_thread(void *arg)
{
  srv_thread_arg_t *args = arg;
  pthread_mutex_lock(&args->res_mutex->mutex);
  *(args->bytes_recv) = 6;
  pthread_mutex_unlock(&args->res_mutex->mutex);

  // Leer datos hasta que se cumpla el tiempo T o se cierre la conexión
  uint8_t buf[MAX_PAYLOAD];
  struct timespec now;
  while (1)
  {
    now = now_ts();
    if (diff_ts(&args->start, &now) >= args->T)
    {
      break;
    }

    ssize_t r = recv(args->conn_fd, buf, sizeof(buf), 0);
    if (r <= 0)
    {
      break;
    }

    pthread_mutex_lock(&args->res_mutex->mutex);
    *(args->bytes_recv) += r;
    *(args->duration) = diff_ts(&args->start, &now);
    pthread_mutex_unlock(&args->res_mutex->mutex);
  }
  close(args->conn_fd);
  return NULL;
}

int server_upload(int N, int T, results_lock_t *results_lock)
{
  struct BW_result *bw_results = results_lock->results;
  printf("server: starting upload test with %d connections for %d seconds...\n",
         N, T);

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    perror("socket");
    return -1;
  }

  int yes = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1)
  {
    perror("setsockopt");
    close(sockfd);
    return -1;
  }

  struct sockaddr_in srv_tcp_addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = INADDR_ANY,
      .sin_port = htons(TCP_PORT_UPLOAD)};

  if (bind(sockfd, (struct sockaddr *)&srv_tcp_addr, sizeof(srv_tcp_addr)) < 0)
  {
    perror("bind");
    close(sockfd);
    return -1;
  }

  if (listen(sockfd, N) < 0)
  {
    perror("listen");
    close(sockfd);
    return -1;
  }

  while (1)
  {
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int conn_fd = accept(sockfd, (struct sockaddr *)&client_addr, &len);
    if (conn_fd < 0)
    {
      perror("accept");
      continue;
    }
    struct timespec start = now_ts();

    uint8_t header[6];
    ssize_t r = recv(conn_fd, header, sizeof(header), 0);
    if (r < 0)
    {
      perror("recv header");
      close(conn_fd);
      continue;
    }

    srv_thread_arg_t *thread_args = malloc(sizeof(*thread_args));
    if (!thread_args)
    {
      perror("malloc");
      close(conn_fd);
      continue;
    }

    uint32_t test_id;
    memcpy(&test_id, header, 4);
    uint16_t conn_id;
    memcpy(&conn_id, header + 4, 2);

    pthread_mutex_lock(&results_lock->mutex);
    int idx = -1;

    uint16_t client_conn = ntohs(conn_id);
    if (client_conn == 1)
    {
      // first sub-connection: grab a free slot
      for (int i = 0; i < MAX_CLIENTS * N; i++)
      {
        if (bw_results[i].id_measurement == 0)
        {
          idx = i;
          bw_results[i].id_measurement = test_id;
          break;
        }
      }
      if (idx < 0)
      {
        fprintf(stderr, "No free slot for new test\n");
      }
    }
    else
    {
      // subsequent sub-connections: find the same slot
      for (int i = 0; i < MAX_CLIENTS * N; i++)
      {
        if (bw_results[i].id_measurement == test_id)
        {
          idx = i;

          break;
        }
      }
      if (idx < 0)
      {
        fprintf(stderr, "Cannot find slot for existing test\n");
      }
    }
    pthread_mutex_unlock(&results_lock->mutex);

    thread_args->conn_fd = conn_fd;
    thread_args->bytes_recv = &(bw_results[idx].conn_bytes[client_conn - 1]);
    thread_args->duration = &(bw_results[idx].conn_duration[client_conn - 1]);
    thread_args->start = start;
    thread_args->T = T;
    thread_args->res_mutex = results_lock;

    pthread_t thread;
    if (pthread_create(&thread, NULL, upload_server_thread, thread_args) != 0)
    {
      fprintf(stderr, "Error creating upload server thread\n");
      free(thread_args);
      close(conn_fd);
      continue;
    }
  }
}

void *upload_client_thread(void *arg)
{
  cli_thread_arg_t *args = arg;

  // Enviar header
  send(args->sockfd, args->header, sizeof(args->header), 0);

  // Crear y llenar buffer de datos
  uint8_t *buf = malloc(args->buf_size);
  if (!buf)
  {
    perror("malloc");
    return NULL;
  }
  memset(buf, 0xAA, args->buf_size);

  // Enviar datos en bucle
  while (1)
  {
    if (send(args->sockfd, buf, args->buf_size, MSG_NOSIGNAL) <= 0)
    {
      break;
    }
  }

  free(buf);
  return NULL;
}

int client_upload(const char *srv_ip, int N, struct BW_result *bw_result)
{
  printf("client: starting upload test to %s:%d with %d connections...\n",
         srv_ip, TCP_PORT_UPLOAD, N);
  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(TCP_PORT_UPLOAD)};
  inet_pton(AF_INET, srv_ip, &srv_addr.sin_addr);

  // Crear y conectar N sockets TCP
  int socks[N];
  for (int i = 0; i < N; i++)
  {
    printf("client: creating socket %d...\n", i);
    socks[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (socks[i] < 0)
    {
      printf("Error creating socket %d\n", i);
      die("socket()");
    }
    if (connect(socks[i], (struct sockaddr *)&srv_addr,
                sizeof(srv_addr)) < 0)
    {
      printf("Error connecting socket %d\n", i);
      die("connect()");
    }
  }

  printf("client: connected %d sockets to server %s:%d\n",
         N, srv_ip, TCP_PORT_UPLOAD);
  // Generar test_id aleatorio
  srand(time(NULL) ^ getpid()); // Seed with current time and PID for more randomness
  uint8_t test_id[4];
  do
  {
    for (int i = 0; i < 4; i++)
    {
      test_id[i] = (uint8_t)rand();
    }
  } while (test_id[0] == 0xFF);

  // Lanzar hilos de subida
  pthread_t threads[N];
  cli_thread_arg_t args[N];

  for (int i = 0; i < N; i++)
  {
    uint8_t header[6];
    memcpy(header, test_id, 4);
    uint16_t cid = htons(i + 1);
    memcpy(header + 4, &cid, 2);
    memcpy(args[i].header, header, 6);

    args[i].sockfd = socks[i];
    args[i].buf_size = MAX_PAYLOAD;

    if (pthread_create(&threads[i], NULL,
                       upload_client_thread,
                       &args[i]) != 0)
    {
      fprintf(stderr, "Error creating upload client thread %d\n", i);
      die("pthread_create()");
    }
    printf("client: started upload thread %d with header %02X%02X%02X%02X%02X%02X\n",
           i, header[0], header[1], header[2], header[3], header[4], header[5]);
  }

  // Esperar a que terminen los hilos
  for (int i = 0; i < N; i++)
  {
    pthread_join(threads[i], NULL);
    close(socks[i]);
  }

  printf("client: all upload threads completed\n");

  // --- Fase UDP: solicitar resultados al servidor ---
  int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_sock < 0)
  {
    die("socket UDP");
  }

  struct sockaddr_in udp_srv = {
      .sin_family = AF_INET,
      .sin_port = htons(UDP_PORT_RESULTS),
  };

  inet_pton(AF_INET, srv_ip, &udp_srv.sin_addr);

  printf("Consulto resultados al servidor UDP %s:%d\n",
         srv_ip, UDP_PORT_RESULTS);

  // Enviar test_id
  ssize_t sent = sendto(udp_sock, test_id, sizeof(test_id), 0,
                        (struct sockaddr *)&udp_srv, sizeof(udp_srv));
  if (sent < 0)
  {
    perror("sendto UDP failed"); // <-- this will print the errno reason
    fprintf(stderr, "  target is %s:%d\n",
            inet_ntoa(udp_srv.sin_addr),
            ntohs(udp_srv.sin_port));
    close(udp_sock);
    return -1;
  }
  // Recibir resultados
  uint8_t buf[MAX_PAYLOAD];
  socklen_t addr_len = sizeof(udp_srv);
  ssize_t r = recvfrom(udp_sock, buf, sizeof(buf), 0,
                       (struct sockaddr *)&udp_srv,
                       &addr_len);
  if (r < 0)
  {
    die("recvfrom UDP");
  }

  printf("Resultados recibidos del servidor UDP: %zd bytes\n", r);

  // Deserializar y mostrar
  if (unpackResultPayload(bw_result, buf, r) < 0)
  {
    fprintf(stderr, "Error unpacking result payload\n");
    close(udp_sock);
    return -1;
  }

  printf("Measurement ID: 0x%02X%02X%02X%02X\n",
         (*bw_result).id_measurement >> 24,
         ((*bw_result).id_measurement >> 16) & 0xFF,
         ((*bw_result).id_measurement >> 8) & 0xFF,
         (*bw_result).id_measurement & 0xFF);

  // Calculate total bytes sent
  uint64_t total_bytes = 0;
  for (int i = 0; i < N; i++)
  {
    printf("Conn %d: bytes=%llu, duration=%.3f s\n",
           i + 1, (unsigned long long)(*bw_result).conn_bytes[i], (*bw_result).conn_duration[i]);
    total_bytes += (*bw_result).conn_bytes[i];
  }

  printf("Total upload bytes: %llu\n", (unsigned long long)total_bytes);

  close(udp_sock);
  return total_bytes; // Return total bytes sent for upload throughput calculation
}
